//============================================================
// Renderer.cpp
//============================================================
#include "Renderer.hpp"

#include <Sysmesh.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <glm/vec3.hpp>
#include <iostream>
#include <vector>

#include "GpuResources/GpuMaterial.hpp"
#include "GpuResources/MeshGpuResources.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "GridRendererVK.hpp"
#include "MeshGpuResources.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "ShaderStage.hpp"
#include "Viewport.hpp"
#include "VkPipelineHelpers.hpp"
#include "VkUtilities.hpp"

namespace
{
    static void imageBarrier(VkCommandBuffer      cmd,
                             VkImage              image,
                             VkImageLayout        oldLayout,
                             VkImageLayout        newLayout,
                             VkAccessFlags        srcAccess,
                             VkAccessFlags        dstAccess,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b{};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                       = oldLayout;
        b.newLayout                       = newLayout;
        b.srcAccessMask                   = srcAccess;
        b.dstAccessMask                   = dstAccess;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = image;
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd,
                             srcStage,
                             dstStage,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &b);
    }

    static void writeTlasDescriptor(VkDevice device, VkDescriptorSet set, VkAccelerationStructureKHR tlas)
    {
        VkWriteDescriptorSetAccelerationStructureKHR asInfo = {};
        asInfo.sType                                        = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount                   = 1;
        asInfo.pAccelerationStructures                      = &tlas;

        VkWriteDescriptorSet write = {};
        write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext                = &asInfo;
        write.dstSet               = set;
        write.dstBinding           = 3;
        write.dstArrayElement      = 0;
        write.descriptorCount      = 1;
        write.descriptorType       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    constexpr bool kRtRebuildAsEveryFrame = false;

    constexpr uint32_t kMaxViewports = 8;

} // namespace

//==================================================================
// RtViewportState destruction
//==================================================================

void Renderer::RtViewportState::destroyDeviceResources(const VulkanContext& ctx) noexcept
{
    for (GpuBuffer& b : cameraBuffers)
        b.destroy();
    cameraBuffers.clear();

    for (GpuBuffer& b : instanceDataBuffers)
        b.destroy();
    instanceDataBuffers.clear();

    if (ctx.device)
    {
        VkDevice device = ctx.device;

        for (RtImagePerFrame& img : images)
        {
            if (img.view)
            {
                vkDestroyImageView(device, img.view, nullptr);
                img.view = VK_NULL_HANDLE;
            }
            if (img.image)
            {
                vkDestroyImage(device, img.image, nullptr);
                img.image = VK_NULL_HANDLE;
            }
            if (img.memory)
            {
                vkFreeMemory(device, img.memory, nullptr);
                img.memory = VK_NULL_HANDLE;
            }

            img.width     = 0;
            img.height    = 0;
            img.needsInit = true;
        }
    }
    images.clear();

    sets.clear();
    cachedW = 0;
    cachedH = 0;
}

//==================================================================
// Init / Lifetime
//==================================================================

Renderer::Renderer() noexcept : m_rtTlasChangeCounter{std::make_shared<SysCounter>()}, m_rtTlasChangeMonitor{m_rtTlasChangeCounter}
{
}

bool Renderer::initDevice(const VulkanContext& ctx)
{
    m_ctx            = ctx;
    m_framesInFlight = std::max(1u, m_ctx.framesInFlight);

    m_viewportUbos.clear();

    m_rtTlasFrames.clear();
    m_rtTlasFrames.resize(m_framesInFlight);

    if (!createDescriptors(m_framesInFlight))
        return false;

    if (!createPipelineLayout())
        return false;

    m_grid = std::make_unique<GridRendererVK>(&m_ctx);
    m_grid->createDeviceResources();

    if (rtReady(m_ctx))
    {
        if (!initRayTracingResources())
        {
            std::cerr << "initRayTracingResources() failed." << std::endl;
            return false;
        }
    }

    return true;
}

bool Renderer::initSwapchain(VkRenderPass renderPass)
{
    destroyPipelines();

    if (!createPipelines(renderPass))
        return false;

    if (m_grid && !m_grid->createPipeline(renderPass, m_pipelineLayout))
        return false;

    if (rtReady(m_ctx))
    {
        if (!createRtPresentPipeline(renderPass))
        {
            std::cerr << "createRtPresentPipeline() failed." << std::endl;
            return false;
        }
    }

    return true;
}

void Renderer::destroySwapchainResources() noexcept
{
    if (m_grid)
        m_grid->destroySwapchainResources();

    destroyRtPresentPipeline();
    destroyPipelines();
}

void Renderer::shutdown() noexcept
{
    destroySwapchainResources();

    // Per-viewport MVP state
    for (auto& [vp, state] : m_viewportUbos)
    {
        for (auto& buf : state.mvpBuffers)
            buf.destroy();
        state.mvpBuffers.clear();
        state.uboSets.clear();
    }
    m_viewportUbos.clear();

    // Per-viewport RT state
    for (auto& [vp, st] : m_rtViewports)
        st.destroyDeviceResources(m_ctx);
    m_rtViewports.clear();

    m_materialBuffer.destroy();
    m_materialSets.clear();

    m_descriptorPool.destroy();
    m_descriptorSetLayout.destroy();
    m_materialSetLayout.destroy();

    // RT device-level resources
    destroyAllRtTlasFrames();
    destroyAllRtBlas();

    m_rtSbt.destroy();
    m_rtPipeline.destroy();

    m_rtScratch.destroy();
    m_rtScratchSize = 0;

    m_rtPool.destroy();
    m_rtSetLayout.destroy();

    if (m_rtSampler != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroySampler(m_ctx.device, m_rtSampler, nullptr);
        m_rtSampler = VK_NULL_HANDLE;
    }

    if (m_rtUploadPool != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyCommandPool(m_ctx.device, m_rtUploadPool, nullptr);
        m_rtUploadPool = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_grid)
        m_grid->destroyDeviceResources();
    m_grid.reset();

    m_overlayVertexBuffer.destroy();
    m_overlayVertexCapacity = 0;

    m_materialCount      = 0;
    m_curMaterialCounter = 0;
    m_framesInFlight     = 0;
    m_ctx                = {};

    m_rtTlasLinkedMeshes.clear();
    m_rtTlasChangeCounter.reset();
}

void Renderer::idle(Scene* scene)
{
    if (!scene)
        return;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm)
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        if (m_rtTlasLinkedMeshes.contains(mesh))
            continue;

        mesh->topology_counter()->addParent(m_rtTlasChangeCounter);
        mesh->deform_counter()->addParent(m_rtTlasChangeCounter);

        m_rtTlasLinkedMeshes.insert(mesh);
    }

    if (m_rtTlasChangeMonitor.changed())
    {
        for (RtTlasFrame& tf : m_rtTlasFrames)
            tf.buildKey = 0;
    }
}

void Renderer::waitDeviceIdle() noexcept
{
    if (!m_ctx.device)
        return;

    vkDeviceWaitIdle(m_ctx.device);
}

//==================================================================
// Pipeline destruction (swapchain-level)
//==================================================================

void Renderer::destroyPipelines() noexcept
{
    if (!m_ctx.device)
        return;

    VkDevice device = m_ctx.device;
    vkDeviceWaitIdle(device);

    auto destroyPipe = [&](VkPipeline& p) noexcept {
        if (p)
        {
            vkDestroyPipeline(device, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    };

    destroyPipe(m_pipelineSolid);
    destroyPipe(m_pipelineShaded);
    destroyPipe(m_pipelineDepthOnly);
    destroyPipe(m_pipelineWire);
    destroyPipe(m_pipelineEdgeHidden);
    destroyPipe(m_pipelineEdgeDepthBias);
    destroyPipe(m_overlayLinePipeline);

    destroyPipe(m_pipelineSelVert);
    destroyPipe(m_pipelineSelEdge);
    destroyPipe(m_pipelineSelPoly);
    destroyPipe(m_pipelineSelVertHidden);
    destroyPipe(m_pipelineSelEdgeHidden);
    destroyPipe(m_pipelineSelPolyHidden);
}

//==================================================================
// Descriptors + pipeline layout (device-level)
//==================================================================

bool Renderer::createDescriptors(uint32_t framesInFlight)
{
    VkDevice device = m_ctx.device;

    DescriptorBindingInfo uboBinding{};
    uboBinding.binding = 0;
    uboBinding.type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.stages  = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
    uboBinding.count   = 1;

    if (!m_descriptorSetLayout.create(device, std::span{&uboBinding, 1}))
    {
        std::cerr << "RendererVK: Failed to create UBO DescriptorSetLayout.\n";
        return false;
    }

    DescriptorBindingInfo matBindings[2]{};

    matBindings[0].binding = 0;
    matBindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    matBindings[0].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    matBindings[0].count   = 1;

    matBindings[1].binding = 1;
    matBindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    matBindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    matBindings[1].count   = kMaxTextureCount;

    if (!m_materialSetLayout.create(device, std::span{matBindings, 2}))
    {
        std::cerr << "RendererVK: Failed to create material DescriptorSetLayout.\n";
        return false;
    }

    std::array<VkDescriptorPoolSize, 3> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, framesInFlight * kMaxViewports},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, framesInFlight * kMaxTextureCount},
    };

    const uint32_t maxSets = (framesInFlight * kMaxViewports) + framesInFlight;

    if (!m_descriptorPool.create(device, poolSizes, maxSets))
    {
        std::cerr << "RendererVK: Failed to create shared DescriptorPool.\n";
        return false;
    }

    m_materialSets.clear();
    m_materialSets.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        if (!m_materialSets[i].allocate(device, m_descriptorPool.pool(), m_materialSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate material DescriptorSet for frame " << i << ".\n";
            return false;
        }
    }

    return true;
}

bool Renderer::createPipelineLayout() noexcept
{
    if (!m_ctx.device)
        return false;

    if (m_pipelineLayout != VK_NULL_HANDLE)
        return true;

    VkDescriptorSetLayout setLayouts[2] = {
        m_descriptorSetLayout.layout(),
        m_materialSetLayout.layout(),
    };

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                         VK_SHADER_STAGE_GEOMETRY_BIT |
                         VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size   = sizeof(PushConstants);

    m_pipelineLayout = vkutil::createPipelineLayout(m_ctx.device, setLayouts, 2, &pcRange, 1);
    return (m_pipelineLayout != VK_NULL_HANDLE);
}

//==================================================================
// Per-viewport MVP UBO (device-level)
//==================================================================

Renderer::ViewportUboState& Renderer::ensureViewportUboState(Viewport* vp)
{
    auto it = m_viewportUbos.find(vp);
    if (it != m_viewportUbos.end())
        return it->second;

    ViewportUboState state{};
    state.mvpBuffers.resize(m_framesInFlight);
    state.uboSets.resize(m_framesInFlight);

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        state.mvpBuffers[i].create(m_ctx.device,
                                   m_ctx.physicalDevice,
                                   sizeof(MvpUBO),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   true);

        if (!state.mvpBuffers[i].valid())
        {
            std::cerr << "RendererVK: Failed to create MVP uniform buffer for viewport at frame " << i << std::endl;
            break;
        }

        if (!state.uboSets[i].allocate(m_ctx.device, m_descriptorPool.pool(), m_descriptorSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate UBO DescriptorSet for viewport at frame " << i << std::endl;
            break;
        }

        state.uboSets[i].writeUniformBuffer(m_ctx.device, 0, state.mvpBuffers[i].buffer(), sizeof(MvpUBO));
    }

    auto [insIt, _] = m_viewportUbos.emplace(vp, std::move(state));
    return insIt->second;
}

//==================================================================
// RT per-viewport state (lazy allocation)
//==================================================================

Renderer::RtViewportState& Renderer::ensureRtViewportState(Viewport* vp)
{
    auto it = m_rtViewports.find(vp);
    if (it != m_rtViewports.end())
        return it->second;

    RtViewportState st{};
    st.sets.resize(m_framesInFlight);
    st.cameraBuffers.resize(m_framesInFlight);
    st.instanceDataBuffers.resize(m_framesInFlight);
    st.images.resize(m_framesInFlight);

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        if (!st.sets[i].allocate(m_ctx.device, m_rtPool.pool(), m_rtSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate RT set for viewport frame " << i << ".\n";
            break;
        }

        st.cameraBuffers[i].create(m_ctx.device,
                                   m_ctx.physicalDevice,
                                   sizeof(RtCameraUBO),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   true);

        if (!st.cameraBuffers[i].valid())
        {
            std::cerr << "RendererVK: Failed to create RT camera UBO for viewport frame " << i << ".\n";
            break;
        }

        // Instance data starts small and grows on upload()
        st.instanceDataBuffers[i].create(m_ctx.device,
                                         m_ctx.physicalDevice,
                                         sizeof(RtInstanceData),
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         false,
                                         false);

        st.sets[i].writeUniformBuffer(m_ctx.device, 2, st.cameraBuffers[i].buffer(), sizeof(RtCameraUBO));
        st.sets[i].writeStorageBuffer(m_ctx.device,
                                      4,
                                      st.instanceDataBuffers[i].buffer(),
                                      st.instanceDataBuffers[i].size(),
                                      0);

        // bindings 0/1 are written by ensureRtOutputImages(...)
        // binding 3 is written by writeRtTlasDescriptor(...)
    }

    auto [insIt, _] = m_rtViewports.emplace(vp, std::move(st));
    return insIt->second;
}

void Renderer::destroyRtOutputImages(RtViewportState& s) noexcept
{
    if (!m_ctx.device)
        return;

    VkDevice device = m_ctx.device;

    for (RtImagePerFrame& img : s.images)
    {
        if (img.view)
        {
            vkDestroyImageView(device, img.view, nullptr);
            img.view = VK_NULL_HANDLE;
        }
        if (img.image)
        {
            vkDestroyImage(device, img.image, nullptr);
            img.image = VK_NULL_HANDLE;
        }
        if (img.memory)
        {
            vkFreeMemory(device, img.memory, nullptr);
            img.memory = VK_NULL_HANDLE;
        }

        img.width     = 0;
        img.height    = 0;
        img.needsInit = true;
    }

    s.cachedW = 0;
    s.cachedH = 0;
}

bool Renderer::ensureRtOutputImages(RtViewportState& s, uint32_t w, uint32_t h)
{
    if (!rtReady(m_ctx))
        return false;

    if (w == 0 || h == 0)
        return false;

    if (s.sets.size() != m_framesInFlight || s.images.size() != m_framesInFlight)
        return false;

    // fast path
    if (s.cachedW == w && s.cachedH == h)
    {
        bool ok = true;
        for (uint32_t i = 0; i < m_framesInFlight; ++i)
        {
            if (!s.images[i].image || !s.images[i].view || s.images[i].width != w || s.images[i].height != h)
            {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }

    destroyRtOutputImages(s);

    VkDevice device = m_ctx.device;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_ctx.physicalDevice, &memProps);

    auto findDeviceLocalType = [&](uint32_t typeBits) noexcept -> uint32_t {
        for (uint32_t m = 0; m < memProps.memoryTypeCount; ++m)
        {
            if ((typeBits & (1u << m)) &&
                (memProps.memoryTypes[m].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                return m;
            }
        }
        return UINT32_MAX;
    };

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        RtImagePerFrame img = {};

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType   = VK_IMAGE_TYPE_2D;
        ici.format      = m_rtFormat;
        ici.extent      = VkExtent3D{w, h, 1};
        ici.mipLevels   = 1;
        ici.arrayLayers = 1;
        ici.samples     = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
        ici.usage       = VK_IMAGE_USAGE_STORAGE_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &ici, nullptr, &img.image) != VK_SUCCESS)
            return false;

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, img.image, &req);

        const uint32_t typeIndex = findDeviceLocalType(req.memoryTypeBits);
        if (typeIndex == UINT32_MAX)
            return false;

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = typeIndex;

        VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flagsInfo.flags = 0;
        mai.pNext       = &flagsInfo;

        if (vkAllocateMemory(device, &mai, nullptr, &img.memory) != VK_SUCCESS)
            return false;

        if (vkBindImageMemory(device, img.image, img.memory, 0) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image                           = img.image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = m_rtFormat;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &vci, nullptr, &img.view) != VK_SUCCESS)
            return false;

        img.width     = w;
        img.height    = h;
        img.needsInit = true;

        s.images[i] = img;

        // Update per-viewport-per-frame RT descriptor set:
        s.sets[i].writeStorageImage(m_ctx.device, 0, s.images[i].view, VK_IMAGE_LAYOUT_GENERAL);
        s.sets[i].writeCombinedImageSampler(m_ctx.device,
                                            1,
                                            m_rtSampler,
                                            s.images[i].view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    s.cachedW = w;
    s.cachedH = h;
    return true;
}

//==================================================================
// RT geometry selection (UNCHANGED from your current file)
//==================================================================

Renderer::RtMeshGeometry Renderer::selectRtGeometry(SceneMesh* sm) noexcept
{
    RtMeshGeometry out = {};

    if (!sm || !sm->gpu())
        return out;

    MeshGpuResources* gpu = sm->gpu();
    if (!gpu)
        return out;

    const bool useSubdiv = (sm->subdivisionLevel() > 0);

    if (!useSubdiv)
    {
        if (gpu->uniqueVertCount() == 0 || !gpu->uniqueVertBuffer().valid())
            return out;
        if (gpu->coarseTriIndexCount() == 0 || !gpu->coarseTriIndexBuffer().valid())
            return out;

        if (gpu->coarseRtPosCount() == 0 || !gpu->coarseRtPosBuffer().valid())
            return out;
        if (gpu->coarseRtCornerNrmCount() == 0 || !gpu->coarseRtCornerNrmBuffer().valid())
            return out;
        if (gpu->coarseRtCornerUvCount() == 0 || !gpu->coarseRtCornerUvBuffer().valid())
            return out;
        if (gpu->coarseRtTriCount() == 0 || !gpu->coarseRtTriIndexBuffer().valid())
            return out;

        out.buildPosBuffer = gpu->uniqueVertBuffer().buffer();
        out.buildPosCount  = gpu->uniqueVertCount();

        out.buildIndexBuffer = gpu->coarseTriIndexBuffer().buffer();
        out.buildIndexCount  = gpu->coarseTriIndexCount();

        out.shadePosBuffer = gpu->coarseRtPosBuffer().buffer();
        out.shadePosCount  = gpu->coarseRtPosCount();

        out.shadeNrmBuffer = gpu->coarseRtCornerNrmBuffer().buffer();
        out.shadeNrmCount  = gpu->coarseRtCornerNrmCount();

        out.shadeUvBuffer = gpu->coarseRtCornerUvBuffer().buffer();
        out.shadeUvCount  = gpu->coarseRtCornerUvCount();

        out.shaderIndexBuffer = gpu->coarseRtTriIndexBuffer().buffer();
        out.shaderTriCount    = gpu->coarseRtTriCount();

        return out;
    }

    if (gpu->subdivSharedVertCount() == 0 || !gpu->subdivSharedVertBuffer().valid())
        return out;
    if (gpu->subdivSharedTriIndexCount() == 0 || !gpu->subdivSharedTriIndexBuffer().valid())
        return out;

    if (gpu->subdivRtPosCount() == 0 || !gpu->subdivRtPosBuffer().valid())
        return out;
    if (gpu->subdivRtCornerNrmCount() == 0 || !gpu->subdivRtCornerNrmBuffer().valid())
        return out;
    if (gpu->subdivRtCornerUvCount() == 0 || !gpu->subdivRtCornerUvBuffer().valid())
        return out;
    if (gpu->subdivRtTriCount() == 0 || !gpu->subdivRtTriIndexBuffer().valid())
        return out;

    out.buildPosBuffer = gpu->subdivSharedVertBuffer().buffer();
    out.buildPosCount  = gpu->subdivSharedVertCount();

    out.buildIndexBuffer = gpu->subdivSharedTriIndexBuffer().buffer();
    out.buildIndexCount  = gpu->subdivSharedTriIndexCount();

    out.shadePosBuffer = gpu->subdivRtPosBuffer().buffer();
    out.shadePosCount  = gpu->subdivRtPosCount();

    out.shadeNrmBuffer = gpu->subdivRtCornerNrmBuffer().buffer();
    out.shadeNrmCount  = gpu->subdivRtCornerNrmCount();

    out.shadeUvBuffer = gpu->subdivRtCornerUvBuffer().buffer();
    out.shadeUvCount  = gpu->subdivRtCornerUvCount();

    out.shaderIndexBuffer = gpu->subdivRtTriIndexBuffer().buffer();
    out.shaderTriCount    = gpu->subdivRtTriCount();

    return out;
}

//==================================================================
// Materials (UNCHANGED from your current file)
//==================================================================

void Renderer::uploadMaterialsToGpu(const std::vector<Material>& materials,
                                    TextureHandler&              texHandler,
                                    uint32_t                     frameIndex)
{
    if (frameIndex >= m_framesInFlight)
        return;

    m_materialCount = static_cast<std::uint32_t>(materials.size());
    if (m_materialCount == 0)
        return;

    std::vector<GpuMaterial> gpuMats;
    buildGpuMaterialArray(materials, texHandler, gpuMats);

    const VkDeviceSize sizeBytes = static_cast<VkDeviceSize>(gpuMats.size() * sizeof(GpuMaterial));

    if (!m_materialBuffer.valid() || m_materialBuffer.size() < sizeBytes)
    {
        m_materialBuffer.destroy();
        m_materialBuffer.create(m_ctx.device,
                                m_ctx.physicalDevice,
                                sizeBytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                false);
    }

    m_materialBuffer.upload(gpuMats.data(), sizeBytes);

    m_materialSets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                  0,
                                                  m_materialBuffer.buffer(),
                                                  sizeBytes,
                                                  0);
}

void Renderer::updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex)
{
    if (frameIndex >= m_framesInFlight)
        return;

    VkDevice device = m_ctx.device;
    if (!device)
        return;

    const int texCount = static_cast<int>(textureHandler.size());
    const int count    = std::min(texCount, static_cast<int>(kMaxTextureCount));

    if (count <= 0)
        return;

    std::vector<VkDescriptorImageInfo> infos;
    infos.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const GpuTexture* tex = textureHandler.get(i);

        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (tex)
        {
            info.imageView = tex->view;
            info.sampler   = tex->sampler;
        }
        else
        {
            info.imageView = VK_NULL_HANDLE;
            info.sampler   = VK_NULL_HANDLE;
        }

        infos.push_back(info);
    }

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_materialSets[frameIndex].set();
    write.dstBinding      = 1;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = static_cast<uint32_t>(infos.size());
    write.pImageInfo      = infos.data();

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

//==================================================================
// Pipelines (swapchain-level)
//==================================================================

bool Renderer::createPipelines(VkRenderPass renderPass)
{
    if (m_pipelineLayout == VK_NULL_HANDLE)
    {
        std::cerr << "RendererVK: createPipelines called before pipeline layout was created.\n";
        return false;
    }

    const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

    ShaderStage SolidDrawVert = vkutil::loadStage(m_ctx.device, shaderDir, "SolidDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage SolidDrawFrag = vkutil::loadStage(m_ctx.device, shaderDir, "SolidDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    ShaderStage ShadedDrawVert = vkutil::loadStage(m_ctx.device, shaderDir, "ShadedDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage ShadedDrawFrag = vkutil::loadStage(m_ctx.device, shaderDir, "ShadedDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    ShaderStage wireVert          = vkutil::loadStage(m_ctx.device, shaderDir, "Wireframe.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage wireFrag          = vkutil::loadStage(m_ctx.device, shaderDir, "Wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    ShaderStage wireDepthBiasVert = vkutil::loadStage(m_ctx.device, shaderDir, "WireframeDepthBias.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);

    ShaderStage overlayVert = vkutil::loadStage(m_ctx.device, shaderDir, "Overlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage overlayGeom = vkutil::loadStage(m_ctx.device, shaderDir, "Overlay.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);
    ShaderStage overlayFrag = vkutil::loadStage(m_ctx.device, shaderDir, "Overlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    ShaderStage selVert     = vkutil::loadStage(m_ctx.device, shaderDir, "Selection.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage selFrag     = vkutil::loadStage(m_ctx.device, shaderDir, "Selection.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    ShaderStage selVertFrag = vkutil::loadStage(m_ctx.device, shaderDir, "SelectionVert.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!SolidDrawVert.isValid() || !SolidDrawFrag.isValid() ||
        !ShadedDrawVert.isValid() || !ShadedDrawFrag.isValid() ||
        !wireVert.isValid() || !wireFrag.isValid() || !wireDepthBiasVert.isValid() ||
        !overlayVert.isValid() || !overlayGeom.isValid() || !overlayFrag.isValid() ||
        !selVert.isValid() || !selFrag.isValid() || !selVertFrag.isValid())
    {
        std::cerr << "RendererVK: Failed to load one or more shader modules.\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo SolidDrawStages[2]  = {SolidDrawVert.stageInfo(), SolidDrawFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo ShadedDrawStages[2] = {ShadedDrawVert.stageInfo(), ShadedDrawFrag.stageInfo()};

    VkPipelineShaderStageCreateInfo wireStages[2]          = {wireVert.stageInfo(), wireFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo wireDepthBiasStages[2] = {wireDepthBiasVert.stageInfo(), wireFrag.stageInfo()};

    VkPipelineShaderStageCreateInfo overlayStages[3] = {overlayVert.stageInfo(), overlayGeom.stageInfo(), overlayFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo selStages[2]     = {selVert.stageInfo(), selFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo selVertStages[2] = {selVert.stageInfo(), selVertFrag.stageInfo()};

    VkVertexInputBindingDescription      solidBindings[4]{};
    VkVertexInputAttributeDescription    solidAttrs[4]{};
    VkPipelineVertexInputStateCreateInfo viSolid{};
    vkutil::makeSolidVertexInput(viSolid, solidBindings, solidAttrs);

    VkVertexInputBindingDescription      lineBinding{};
    VkVertexInputAttributeDescription    lineAttr{};
    VkPipelineVertexInputStateCreateInfo viLines{};
    vkutil::makeLineVertexInput(viLines, lineBinding, lineAttr);

    VkVertexInputBindingDescription overlayBinding{};
    overlayBinding.binding   = 0;
    overlayBinding.stride    = sizeof(OverlayVertex);
    overlayBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription overlayAttrs[3]{};

    overlayAttrs[0].location = 0;
    overlayAttrs[0].binding  = 0;
    overlayAttrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    overlayAttrs[0].offset   = offsetof(OverlayVertex, pos);

    overlayAttrs[1].location = 1;
    overlayAttrs[1].binding  = 0;
    overlayAttrs[1].format   = VK_FORMAT_R32_SFLOAT;
    overlayAttrs[1].offset   = offsetof(OverlayVertex, thickness);

    overlayAttrs[2].location = 2;
    overlayAttrs[2].binding  = 0;
    overlayAttrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    overlayAttrs[2].offset   = offsetof(OverlayVertex, color);

    VkPipelineVertexInputStateCreateInfo viOverlay{};
    viOverlay.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viOverlay.vertexBindingDescriptionCount   = 1;
    viOverlay.pVertexBindingDescriptions      = &overlayBinding;
    viOverlay.vertexAttributeDescriptionCount = 3;
    viOverlay.pVertexAttributeDescriptions    = overlayAttrs;

    vkutil::MeshPipelinePreset solidPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = true,
        .depthCompareOp      = VK_COMPARE_OP_LESS,
        .enableBlend         = false,
        .enableDepthBias     = false,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset wirePreset{
        .topology              = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode           = VK_POLYGON_MODE_FILL,
        .cullMode              = VK_CULL_MODE_NONE,
        .frontFace             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest             = true,
        .depthWrite            = false,
        .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend           = true,
        .enableDepthBias       = false,
        .colorWrite            = true,
        .sampleShadingEnable   = false,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = false,
    };

    vkutil::MeshPipelinePreset edgeOverlayPreset = wirePreset;

    vkutil::MeshPipelinePreset depthOnlyPreset = solidPreset;
    depthOnlyPreset.enableBlend                = false;
    depthOnlyPreset.depthWrite                 = true;
    depthOnlyPreset.depthCompareOp             = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthOnlyPreset.colorWrite                 = false;

    vkutil::MeshPipelinePreset hiddenEdgePreset = wirePreset;
    hiddenEdgePreset.depthCompareOp             = VK_COMPARE_OP_GREATER;
    hiddenEdgePreset.depthWrite                 = false;

    vkutil::MeshPipelinePreset overlayPreset{
        .topology              = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode           = VK_POLYGON_MODE_FILL,
        .cullMode              = VK_CULL_MODE_NONE,
        .frontFace             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest             = false,
        .depthWrite            = false,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .enableBlend           = true,
        .enableDepthBias       = false,
        .colorWrite            = true,
        .sampleShadingEnable   = false,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = false,
    };

    vkutil::MeshPipelinePreset selVertPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selEdgePreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selPolyPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selVertHiddenPreset = selVertPreset;
    selVertHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    vkutil::MeshPipelinePreset selEdgeHiddenPreset = selEdgePreset;
    selEdgeHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    vkutil::MeshPipelinePreset selPolyHiddenPreset = selPolyPreset;
    selPolyHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    m_pipelineSolid = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, SolidDrawStages, 2, &viSolid, solidPreset);
    if (!m_pipelineSolid)
    {
        std::cerr << "RendererVK: createMeshPipeline(solid) failed.\n";
        return false;
    }

    m_pipelineShaded = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, ShadedDrawStages, 2, &viSolid, solidPreset);
    if (!m_pipelineShaded)
    {
        std::cerr << "RendererVK: createMeshPipeline(shaded) failed.\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo depthStages[1] = {SolidDrawVert.stageInfo()};
    m_pipelineDepthOnly                            = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, depthStages, 1, &viSolid, depthOnlyPreset);
    if (!m_pipelineDepthOnly)
    {
        std::cerr << "RendererVK: createMeshPipeline(depthOnly) failed.\n";
        return false;
    }

    m_pipelineWire = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, wireStages, 2, &viLines, wirePreset);
    if (!m_pipelineWire)
    {
        std::cerr << "RendererVK: createMeshPipeline(wire) failed.\n";
        return false;
    }

    m_pipelineEdgeHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, wireStages, 2, &viLines, hiddenEdgePreset);
    if (!m_pipelineEdgeHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(edgeHidden) failed.\n";
        return false;
    }

    m_pipelineEdgeDepthBias = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, wireDepthBiasStages, 2, &viLines, edgeOverlayPreset);
    if (!m_pipelineEdgeDepthBias)
    {
        std::cerr << "RendererVK: createMeshPipeline(edgeOverlay) failed.\n";
        return false;
    }

    m_overlayLinePipeline = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, overlayStages, 3, &viOverlay, overlayPreset);
    if (!m_overlayLinePipeline)
    {
        std::cerr << "RendererVK: createMeshPipeline(overlay) failed.\n";
        return false;
    }

    m_pipelineSelVert = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selVertStages, 2, &viLines, selVertPreset);
    if (!m_pipelineSelVert)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection verts) failed.\n";
        return false;
    }

    m_pipelineSelEdge = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selEdgePreset);
    if (!m_pipelineSelEdge)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection edges) failed.\n";
        return false;
    }

    m_pipelineSelPoly = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selPolyPreset);
    if (!m_pipelineSelPoly)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection polys) failed.\n";
        return false;
    }

    m_pipelineSelVertHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selVertStages, 2, &viLines, selVertHiddenPreset);
    if (!m_pipelineSelVertHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection verts hidden) failed.\n";
        return false;
    }

    m_pipelineSelEdgeHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selEdgeHiddenPreset);
    if (!m_pipelineSelEdgeHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection edges hidden) failed.\n";
        return false;
    }

    m_pipelineSelPolyHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selPolyHiddenPreset);
    if (!m_pipelineSelPolyHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection polys hidden) failed.\n";
        return false;
    }

    return true;
}

//==================================================================
// RT present pipeline (swapchain-level)
//==================================================================

bool Renderer::createRtPresentPipeline(VkRenderPass renderPass)
{
    destroyRtPresentPipeline();

    if (!rtReady(m_ctx))
        return true;

    if (!m_rtSetLayout.layout())
    {
        std::cerr << "RendererVK: RT set layout not created yet.\n";
        return false;
    }

    const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

    ShaderStage vs = vkutil::loadStage(m_ctx.device, shaderDir, "RtPresent.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage fs = vkutil::loadStage(m_ctx.device, shaderDir, "RtPresent.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!vs.isValid() || !fs.isValid())
    {
        std::cerr << "RendererVK: Failed to load RtPresent shaders.\n";
        return false;
    }

    VkDescriptorSetLayout setLayouts[1] = {m_rtSetLayout.layout()};

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = setLayouts;

    if (vkCreatePipelineLayout(m_ctx.device, &plci, nullptr, &m_rtPresentLayout) != VK_SUCCESS)
    {
        std::cerr << "RendererVK: vkCreatePipelineLayout(RtPresent) failed.\n";
        destroyRtPresentPipeline();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {vs.stageInfo(), fs.stageInfo()};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = m_ctx.sampleCount;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                           VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cbAtt;

    VkDynamicState                   dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dyn;
    gp.layout              = m_rtPresentLayout;
    gp.renderPass          = renderPass;
    gp.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_ctx.device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_rtPresentPipeline) != VK_SUCCESS)
    {
        std::cerr << "RendererVK: vkCreateGraphicsPipelines(RtPresent) failed.\n";
        destroyRtPresentPipeline();
        return false;
    }

    return true;
}

void Renderer::destroyRtPresentPipeline() noexcept
{
    if (!m_ctx.device)
        return;

    VkDevice device = m_ctx.device;

    if (m_rtPresentPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_rtPresentPipeline, nullptr);
        m_rtPresentPipeline = VK_NULL_HANDLE;
    }

    if (m_rtPresentLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_rtPresentLayout, nullptr);
        m_rtPresentLayout = VK_NULL_HANDLE;
    }
}

//==================================================================
// RT init (device-level) - now ONLY creates layout/pool/pipeline/sbt/sampler
// (sets + camera buffers + images are per-viewport, lazy)
//==================================================================

bool Renderer::initRayTracingResources()
{
    if (!rtReady(m_ctx))
        return false;

    VkDevice device = m_ctx.device;
    if (!device)
        return false;

    DescriptorBindingInfo bindings[5] = {};

    bindings[0].binding = 0;
    bindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[0].count   = 1;

    bindings[1].binding = 1;
    bindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].count   = 1;

    bindings[2].binding = 2;
    bindings[2].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[2].count   = 1;

    bindings[3].binding = 3;
    bindings[3].type    = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[3].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[3].count   = 1;

    bindings[4].binding = 4;
    bindings[4].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[4].count   = 1;

    if (!m_rtSetLayout.create(device, std::span{bindings, 5}))
    {
        std::cerr << "RendererVK: Failed to create RT DescriptorSetLayout.\n";
        return false;
    }

    // Pool is sized for (frames * maxViewports)
    const uint32_t setCount = std::max(1u, m_framesInFlight) * kMaxViewports;

    std::array<VkDescriptorPoolSize, 5> poolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount},
    };

    if (!m_rtPool.create(device, poolSizes, /*maxSets*/ setCount))
    {
        std::cerr << "RendererVK: Failed to create RT DescriptorPool.\n";
        return false;
    }

    if (m_rtSampler == VK_NULL_HANDLE)
    {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = VK_FILTER_LINEAR;
        sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod       = 0.0f;

        if (vkCreateSampler(device, &sci, nullptr, &m_rtSampler) != VK_SUCCESS)
        {
            std::cerr << "RendererVK: Failed to create RT present sampler.\n";
            return false;
        }
    }

    if (!m_rtPipeline.createScenePipeline(m_ctx, m_rtSetLayout.layout()))
    {
        std::cerr << "RendererVK: Failed to create RT scene pipeline.\n";
        return false;
    }

    if (m_rtUploadPool == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_ctx.graphicsQueueFamilyIndex;

        if (vkCreateCommandPool(device, &pci, nullptr, &m_rtUploadPool) != VK_SUCCESS)
        {
            std::cerr << "RendererVK: Failed to create RT upload command pool.\n";
            return false;
        }
    }

    if (!m_rtSbt.buildAndUpload(m_ctx,
                                m_rtPipeline.pipeline(),
                                vkrt::RtPipeline::kRaygenCount,
                                vkrt::RtPipeline::kMissCount,
                                vkrt::RtPipeline::kHitCount,
                                vkrt::RtPipeline::kCallableCount,
                                m_rtUploadPool,
                                m_ctx.graphicsQueue))
    {
        std::cerr << "RendererVK: Failed to build/upload SBT.\n";
        return false;
    }

    return true;
}

//==================================================================
// RT scratch
//==================================================================

bool Renderer::ensureRtScratch(VkDeviceSize bytes) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device)
        return false;

    if (bytes == 0)
        return false;

    if (m_rtScratch.valid() && m_rtScratch.size() >= bytes)
        return true;

    m_rtScratch.destroy();
    m_rtScratchSize = 0;

    m_rtScratch.create(m_ctx.device,
                       m_ctx.physicalDevice,
                       bytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       false,
                       true);

    if (!m_rtScratch.valid())
        return false;

    m_rtScratchSize = bytes;
    return true;
}

//==================================================================
// RT AS teardown (UNCHANGED from your current file)
//==================================================================

void Renderer::destroyRtBlasFor(SceneMesh* sm) noexcept
{
    auto it = m_rtBlas.find(sm);
    if (it == m_rtBlas.end())
        return;

    RtBlas& b = it->second;

    if (b.as != VK_NULL_HANDLE)
    {
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);
        b.as = VK_NULL_HANDLE;
    }

    b.asBuffer.destroy();
    b.address = 0;

    m_rtBlas.erase(it);
}

void Renderer::destroyAllRtBlas() noexcept
{
    if (!m_ctx.device || !m_ctx.rtDispatch)
        return;

    for (auto& [sm, b] : m_rtBlas)
    {
        if (b.as != VK_NULL_HANDLE)
            m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);

        b.as = VK_NULL_HANDLE;
        b.asBuffer.destroy();
        b.address  = 0;
        b.buildKey = 0;
    }

    m_rtBlas.clear();
}

void Renderer::destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept
{
    if (frameIndex >= m_rtTlasFrames.size())
        return;

    RtTlasFrame& t = m_rtTlasFrames[frameIndex];

    if (rtReady(m_ctx) && m_ctx.rtDispatch && m_ctx.device && t.as)
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, t.as, nullptr);

    t.as       = VK_NULL_HANDLE;
    t.address  = 0;
    t.buildKey = 0;

    t.buffer.destroy();

    if (destroyInstanceBuffers)
    {
        t.instanceBuffer.destroy();
        t.instanceStaging.destroy();
    }
}

void Renderer::destroyAllRtTlasFrames() noexcept
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_rtTlasFrames.size()); ++i)
        destroyRtTlasFrame(i, true);

    m_rtTlasFrames.clear();
}

//==================================================================
// Render (RT present now uses per-viewport RT set/image)
//==================================================================

void Renderer::render(VkCommandBuffer cmd, Viewport* vp, Scene* scene, uint32_t frameIndex)
{
    if (!vp || !scene)
        return;

    if (frameIndex >= m_framesInFlight)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());

    const glm::vec4 solidEdgeColor{0.10f, 0.10f, 0.10f, 0.5f};
    const glm::vec4 wireVisibleColor{0.85f, 0.85f, 0.85f, 1.0f};
    const glm::vec4 wireHiddenColor{0.85f, 0.85f, 0.85f, 0.25f};

    // ------------------------------------------------------------
    // RAY TRACE PRESENT PATH (EARLY OUT) - PER VIEWPORT
    // ------------------------------------------------------------
    if (vp->drawMode() == DrawMode::RAY_TRACE)
    {
        if (!rtReady(m_ctx))
            return;

        RtViewportState& rtv = ensureRtViewportState(vp);

        if (!ensureRtOutputImages(rtv, w, h))
            return;

        if (!m_rtPresentPipeline || !m_rtPresentLayout)
            return;

        if (frameIndex >= rtv.sets.size())
            return;

        vkutil::setViewportAndScissor(cmd, w, h);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rtPresentPipeline);

        VkDescriptorSet rtSet0 = rtv.sets[frameIndex].set();
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_rtPresentLayout,
                                0,
                                1,
                                &rtSet0,
                                0,
                                nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        // IMPORTANT: restore normal graphics set=0 binding/layout.
        {
            ViewportUboState& vpUbo = ensureViewportUboState(vp);

            if (frameIndex < vpUbo.mvpBuffers.size() &&
                frameIndex < vpUbo.uboSets.size() &&
                vpUbo.mvpBuffers[frameIndex].valid())
            {
                MvpUBO ubo{};
                ubo.proj = vp->projection();
                ubo.view = vp->view();
                vpUbo.mvpBuffers[frameIndex].upload(&ubo, sizeof(ubo));

                VkDescriptorSet gfxSet0 = vpUbo.uboSets[frameIndex].set();
                vkCmdBindDescriptorSets(cmd,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_pipelineLayout,
                                        0,
                                        1,
                                        &gfxSet0,
                                        0,
                                        nullptr);
            }
        }

        return;
    }

    // ------------------------------------------------------------
    // NORMAL GRAPHICS PATH (bind MVP set=0)
    // ------------------------------------------------------------

    ViewportUboState& vpUbo = ensureViewportUboState(vp);

    if (frameIndex >= vpUbo.mvpBuffers.size() || frameIndex >= vpUbo.uboSets.size())
        return;

    if (!vpUbo.mvpBuffers[frameIndex].valid())
        return;

    MvpUBO ubo{};
    ubo.proj = vp->projection();
    ubo.view = vp->view();
    vpUbo.mvpBuffers[frameIndex].upload(&ubo, sizeof(ubo));

    vkutil::setViewportAndScissor(cmd, w, h);

    {
        VkDescriptorSet set0 = vpUbo.uboSets[frameIndex].set();
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout,
                                0,
                                1,
                                &set0,
                                0,
                                nullptr);
    }

    // ------------------------------------------------------------
    // Solid / Shaded
    // ------------------------------------------------------------
    if (vp->drawMode() != DrawMode::WIREFRAME)
    {
        const bool isShaded = (vp->drawMode() == DrawMode::SHADED);

        VkPipeline triPipe = isShaded ? m_pipelineShaded : m_pipelineSolid;

        if (scene->materialHandler() && m_curMaterialCounter != scene->materialHandler()->changeCounter()->value())
        {
            const auto& mats = scene->materialHandler()->materials();
            for (uint32_t i = 0; i < m_ctx.framesInFlight; ++i)
            {
                uploadMaterialsToGpu(mats, *scene->textureHandler(), i);
                updateMaterialTextureTable(*scene->textureHandler(), i);
            }
            m_curMaterialCounter = scene->materialHandler()->changeCounter()->value();
        }

        {
            VkDescriptorSet set1 = m_materialSets[frameIndex].set();
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout,
                                    1,
                                    1,
                                    &set1,
                                    0,
                                    nullptr);
        }

        if (triPipe)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipe);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 1);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->vertexCount() == 0)
                        continue;

                    if (!gpu->polyVertBuffer().valid() ||
                        !gpu->polyNormBuffer().valid() ||
                        !gpu->polyUvPosBuffer().valid() ||
                        !gpu->polyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->polyVertBuffer().buffer(),
                        gpu->polyNormBuffer().buffer(),
                        gpu->polyUvPosBuffer().buffer(),
                        gpu->polyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->vertexCount(), 1, 0, 0);
                }
                else
                {
                    if (gpu->subdivPolyVertexCount() == 0)
                        continue;

                    if (!gpu->subdivPolyVertBuffer().valid() ||
                        !gpu->subdivPolyNormBuffer().valid() ||
                        !gpu->subdivPolyUvBuffer().valid() ||
                        !gpu->subdivPolyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->subdivPolyVertBuffer().buffer(),
                        gpu->subdivPolyNormBuffer().buffer(),
                        gpu->subdivPolyUvBuffer().buffer(),
                        gpu->subdivPolyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->subdivPolyVertexCount(), 1, 0, 0);
                }
            }
        }

        constexpr bool drawEdgesInSolid = true;
        if (!isShaded && drawEdgesInSolid && m_pipelineEdgeDepthBias)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineEdgeDepthBias);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = solidEdgeColor;

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->edgeIndexCount() == 0)
                        continue;

                    if (!gpu->uniqueVertBuffer().valid() || !gpu->edgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->uniqueVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->edgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->edgeIndexCount(), 1, 0, 0, 0);
                }
                else
                {
                    if (gpu->subdivPrimaryEdgeIndexCount() == 0)
                        continue;

                    if (!gpu->subdivSharedVertBuffer().valid() || !gpu->subdivPrimaryEdgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->subdivSharedVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->subdivPrimaryEdgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->subdivPrimaryEdgeIndexCount(), 1, 0, 0, 0);
                }
            }
        }
    }
    // ------------------------------------------------------------
    // Wireframe mode (hidden-line)
    // ------------------------------------------------------------
    else
    {
        // 1) depth-only triangles
        if (m_pipelineDepthOnly)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineDepthOnly);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 0);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT |
                                       VK_SHADER_STAGE_GEOMETRY_BIT |
                                       VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->vertexCount() == 0)
                        continue;

                    if (!gpu->polyVertBuffer().valid() ||
                        !gpu->polyNormBuffer().valid() ||
                        !gpu->polyUvPosBuffer().valid() ||
                        !gpu->polyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->polyVertBuffer().buffer(),
                        gpu->polyNormBuffer().buffer(),
                        gpu->polyUvPosBuffer().buffer(),
                        gpu->polyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->vertexCount(), 1, 0, 0);
                }
                else
                {
                    if (gpu->subdivPolyVertexCount() == 0)
                        continue;

                    if (!gpu->subdivPolyVertBuffer().valid() ||
                        !gpu->subdivPolyNormBuffer().valid() ||
                        !gpu->subdivPolyUvBuffer().valid() ||
                        !gpu->subdivPolyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->subdivPolyVertBuffer().buffer(),
                        gpu->subdivPolyNormBuffer().buffer(),
                        gpu->subdivPolyUvBuffer().buffer(),
                        gpu->subdivPolyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->subdivPolyVertexCount(), 1, 0, 0);
                }
            }
        }

        auto drawEdges = [&](VkPipeline pipeline, const glm::vec4& color) {
            if (!pipeline)
                return;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = color;

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->edgeIndexCount() == 0)
                        continue;

                    if (!gpu->uniqueVertBuffer().valid() || !gpu->edgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->uniqueVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->edgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->edgeIndexCount(), 1, 0, 0, 0);
                }
                else
                {
                    if (gpu->subdivPrimaryEdgeIndexCount() == 0)
                        continue;

                    if (!gpu->subdivSharedVertBuffer().valid() || !gpu->subdivPrimaryEdgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->subdivSharedVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->subdivPrimaryEdgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->subdivPrimaryEdgeIndexCount(), 1, 0, 0, 0);
                }
            }
        };

        // 2) hidden edges (GREATER) dim
        drawEdges(m_pipelineEdgeHidden, wireHiddenColor);

        // 3) visible edges (LEQUAL) normal
        drawEdges(m_pipelineWire, wireVisibleColor);
    }

    // Selection overlay
    drawSelection(cmd, vp, scene);

    // Scene Grid (draw last) - NOT in SHADED mode
    if (scene->showSceneGrid() && vp->drawMode() != DrawMode::SHADED)
    {
        drawSceneGrid(cmd, vp, scene);
    }
}

//==================================================================
// RT dispatch (now fully per-viewport)
//==================================================================

void Renderer::writeRtTlasDescriptor(Viewport* vp, uint32_t frameIndex) noexcept
{
    if (!vp)
        return;

    if (frameIndex >= m_rtTlasFrames.size())
        return;

    RtTlasFrame& tf = m_rtTlasFrames[frameIndex];
    if (tf.as == VK_NULL_HANDLE)
        return;

    RtViewportState& rtv = ensureRtViewportState(vp);
    if (frameIndex >= rtv.sets.size())
        return;

    writeTlasDescriptor(m_ctx.device, rtv.sets[frameIndex].set(), tf.as);
}

void Renderer::renderRayTrace(Viewport* vp, VkCommandBuffer cmd, Scene* scene, uint32_t frameIndex)
{
    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return;

    if (!cmd || !vp || !scene)
        return;

    if (!m_rtPipeline.valid() || !m_rtSbt.buffer())
        return;

    if (frameIndex >= m_framesInFlight)
        return;

    RtViewportState& rtv = ensureRtViewportState(vp);

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());
    if (w == 0 || h == 0)
        return;

    if (!ensureRtOutputImages(rtv, w, h))
        return;

    if (frameIndex >= rtv.images.size() || frameIndex >= rtv.cameraBuffers.size())
        return;

    RtImagePerFrame& out = rtv.images[frameIndex];
    if (!out.image || !out.view)
        return;

    // Clear RT output to viewport background
    {
        const VkClearColorValue clear = vkutil::toVkClearColor(vp->clearColor());

        VkImageSubresourceRange range = {};
        range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel            = 0;
        range.levelCount              = 1;
        range.baseArrayLayer          = 0;
        range.layerCount              = 1;

        if (out.needsInit)
        {
            imageBarrier(cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_GENERAL,
                         0,
                         VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);

            out.needsInit = false;
        }
        else
        {
            imageBarrier(cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        vkCmdClearColorImage(cmd, out.image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);

        imageBarrier(cmd,
                     out.image,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    // Build BLAS for visible meshes
    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        if (!sm->gpu())
            sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

        MeshGpuResources* gpu = sm->gpu();
        if (!gpu)
            continue;

        gpu->update();

        const RtMeshGeometry geo = selectRtGeometry(sm);
        if (!geo.valid())
            continue;

        if (!ensureMeshBlas(sm, geo, cmd))
            continue;
    }

    if (!ensureSceneTlas(scene, cmd, frameIndex))
        return;

    if (frameIndex >= m_rtTlasFrames.size() || m_rtTlasFrames[frameIndex].as == VK_NULL_HANDLE)
        return;

    // Bind TLAS into THIS viewport’s RT set for this frame
    writeRtTlasDescriptor(vp, frameIndex);

    // Upload per-instance shader data
    {
        std::vector<RtInstanceData> instData;
        instData.reserve(scene->sceneMeshes().size());

        for (SceneMesh* sm : scene->sceneMeshes())
        {
            if (!sm || !sm->visible())
                continue;

            auto it = m_rtBlas.find(sm);
            if (it == m_rtBlas.end())
                continue;

            const RtBlas& b = it->second;
            if (b.as == VK_NULL_HANDLE || b.address == 0)
                continue;

            const RtMeshGeometry geo = selectRtGeometry(sm);
            if (!geo.valid() || !geo.shaderValid())
                continue;

            const uint32_t primCount = geo.buildIndexCount / 3u;
            if (primCount == 0)
                continue;

            if (geo.shaderTriCount != primCount)
                continue;

            if (geo.shadeNrmCount != primCount * 3u)
                continue;

            RtInstanceData d = {};
            d.posAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadePosBuffer);
            d.idxAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shaderIndexBuffer);
            d.nrmAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeNrmBuffer);
            d.uvAdr          = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeUvBuffer);
            d.triCount       = geo.shaderTriCount;

            if (d.posAdr == 0 || d.idxAdr == 0 || d.nrmAdr == 0 || d.triCount == 0)
                continue;

            instData.push_back(d);
        }

        if (!instData.empty())
        {
            const VkDeviceSize bytes = VkDeviceSize(instData.size() * sizeof(RtInstanceData));
            rtv.instanceDataBuffers[frameIndex].upload(instData.data(), bytes);

            rtv.sets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                    4,
                                                    rtv.instanceDataBuffers[frameIndex].buffer(),
                                                    bytes,
                                                    0);
        }
    }

    // Update RT camera UBO (per viewport)
    {
        RtCameraUBO cam = {};
        cam.invViewProj = glm::inverse(vp->projection() * vp->view());
        cam.camPos      = glm::vec4(vp->cameraPosition(), 1.0f);
        rtv.cameraBuffers[frameIndex].upload(&cam, sizeof(cam));
    }

    // Transition for raygen writes
    imageBarrier(cmd,
                 out.image,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_SHADER_READ_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline.pipeline());

    VkDescriptorSet set0 = rtv.sets[frameIndex].set();
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_rtPipeline.layout(),
                            0,
                            1,
                            &set0,
                            0,
                            nullptr);

    VkStridedDeviceAddressRegionKHR rgen = {};
    VkStridedDeviceAddressRegionKHR miss = {};
    VkStridedDeviceAddressRegionKHR hit  = {};
    VkStridedDeviceAddressRegionKHR call = {};
    m_rtSbt.regions(m_ctx, rgen, miss, hit, call);

    m_ctx.rtDispatch->vkCmdTraceRaysKHR(cmd,
                                        &rgen,
                                        &miss,
                                        &hit,
                                        &call,
                                        w,
                                        h,
                                        1);

    // Transition back for present sampling
    imageBarrier(cmd,
                 out.image,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void Renderer::renderPrePass(Viewport* vp, VkCommandBuffer cmd, Scene* scene, uint32_t frameIndex)
{
    if (!vp || !cmd || !scene)
        return;

    if (vp->drawMode() != DrawMode::RAY_TRACE)
        return;

    if (!rtReady(m_ctx))
        return;

    renderRayTrace(vp, cmd, scene, frameIndex);
}

bool Renderer::ensureMeshBlas(SceneMesh* sm, const RtMeshGeometry& geo, VkCommandBuffer cmd) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !sm || !cmd)
        return false;

    if (!geo.valid() || geo.buildIndexCount == 0 || geo.buildPosCount == 0)
        return false;

    // Build key: topology+deform counters + geometry sizes.
    // If you have better mesh/gpu counters, replace this. This works with SysMesh counters.
    uint64_t topo   = 0;
    uint64_t deform = 0;
    if (sm->sysMesh())
    {
        if (sm->sysMesh()->topology_counter())
            topo = sm->sysMesh()->topology_counter()->value();
        if (sm->sysMesh()->deform_counter())
            deform = sm->sysMesh()->deform_counter()->value();
    }

    // Mix into a cheap key.
    uint64_t key = topo;
    key ^= (deform + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
    key ^= (uint64_t(geo.buildPosCount) << 32) ^ uint64_t(geo.buildIndexCount);

    RtBlas& b = m_rtBlas[sm];

    if (!kRtRebuildAsEveryFrame && b.as != VK_NULL_HANDLE && b.buildKey == key)
        return true;

    // Tear down existing BLAS
    if (b.as != VK_NULL_HANDLE)
    {
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);
        b.as = VK_NULL_HANDLE;
    }
    b.asBuffer.destroy();
    b.address  = 0;
    b.buildKey = 0;

    // Geometry description
    const VkDeviceAddress vAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildPosBuffer);
    const VkDeviceAddress iAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildIndexBuffer);

    if (vAdr == 0 || iAdr == 0)
        return false;

    VkAccelerationStructureGeometryKHR asGeom{};
    asGeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureGeometryTrianglesDataKHR tri{};
    tri.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = vAdr;
    tri.vertexStride             = sizeof(glm::vec3); // your unique/shared buffers are vec3 tightly packed
    tri.maxVertex                = geo.buildPosCount ? (geo.buildPosCount - 1) : 0;
    tri.indexType                = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress  = iAdr;

    asGeom.geometry.triangles = tri;

    const uint32_t primCount = geo.buildIndexCount / 3u;
    if (primCount == 0)
        return false;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &asGeom;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    m_ctx.rtDispatch->vkGetAccelerationStructureBuildSizesKHR(m_ctx.device,
                                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                              &buildInfo,
                                                              &primCount,
                                                              &sizeInfo);

    if (sizeInfo.accelerationStructureSize == 0 || sizeInfo.buildScratchSize == 0)
        return false;

    // Create buffer backing the BLAS
    b.asBuffer.create(m_ctx.device,
                      m_ctx.physicalDevice,
                      sizeInfo.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      false,
                      true);

    if (!b.asBuffer.valid())
        return false;

    VkAccelerationStructureCreateInfoKHR asci{};
    asci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asci.size   = sizeInfo.accelerationStructureSize;
    asci.buffer = b.asBuffer.buffer();

    if (m_ctx.rtDispatch->vkCreateAccelerationStructureKHR(m_ctx.device, &asci, nullptr, &b.as) != VK_SUCCESS)
        return false;

    // Scratch
    if (!ensureRtScratch(sizeInfo.buildScratchSize))
        return false;

    buildInfo.dstAccelerationStructure  = b.as;
    buildInfo.scratchData.deviceAddress = vkutil::bufferDeviceAddress(m_ctx.device, m_rtScratch.buffer());

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount                                      = primCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRanges[] = {&range};

    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, pRanges);

    // Barrier: BLAS build writes -> RT reads
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         1,
                         &mb,
                         0,
                         nullptr,
                         0,
                         nullptr);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = b.as;

    b.address  = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    b.buildKey = key;

    return (b.address != 0);
}

bool Renderer::ensureSceneTlas(Scene* scene, VkCommandBuffer cmd, uint32_t frameIndex) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !scene || !cmd)
        return false;

    if (frameIndex >= m_rtTlasFrames.size())
        return false;

    RtTlasFrame& t = m_rtTlasFrames[frameIndex];

    // Use your change counter as the TLAS rebuild key.
    // You already reset buildKey=0 in idle() when monitor changes.
    const uint64_t key = m_rtTlasChangeCounter ? m_rtTlasChangeCounter->value() : 1;

    if (!kRtRebuildAsEveryFrame && t.as != VK_NULL_HANDLE && t.buildKey == key)
        return true;

    // Gather instances (must match the order used for RtInstanceData upload!)
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(scene->sceneMeshes().size());

    // Also gather BLAS refs in the same order for any debugging/consistency checks
    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        auto it = m_rtBlas.find(sm);
        if (it == m_rtBlas.end())
            continue;

        const RtBlas& b = it->second;
        if (b.as == VK_NULL_HANDLE || b.address == 0)
            continue;

        VkAccelerationStructureInstanceKHR inst{};
        // Row-major 3x4; identity (no transforms yet)
        inst.transform.matrix[0][0] = 1.0f;
        inst.transform.matrix[1][1] = 1.0f;
        inst.transform.matrix[2][2] = 1.0f;

        // These must match your shader's instance indexing assumptions:
        // customIndex typically maps to "instanceId" used in closest hit.
        inst.instanceCustomIndex                    = static_cast<uint32_t>(instances.size());
        inst.mask                                   = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0; // 1 hit group for now
        inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference         = b.address;

        instances.push_back(inst);
    }

    if (instances.empty())
    {
        // No geometry: destroy TLAS if it exists.
        destroyRtTlasFrame(frameIndex, false);
        t.buildKey = key;
        return true;
    }

    const VkDeviceSize instanceBytes = VkDeviceSize(instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

    // Ensure staging buffer (host visible)
    if (!t.instanceStaging.valid() || t.instanceStaging.size() < instanceBytes)
    {
        t.instanceStaging.destroy();
        t.instanceStaging.create(m_ctx.device,
                                 m_ctx.physicalDevice,
                                 instanceBytes,
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 true);
        if (!t.instanceStaging.valid())
            return false;
    }

    // Ensure device-local instance buffer (for build input)
    if (!t.instanceBuffer.valid() || t.instanceBuffer.size() < instanceBytes)
    {
        t.instanceBuffer.destroy();
        t.instanceBuffer.create(m_ctx.device,
                                m_ctx.physicalDevice,
                                instanceBytes,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                false,
                                true);
        if (!t.instanceBuffer.valid())
            return false;
    }

    // Upload instances -> staging
    t.instanceStaging.upload(instances.data(), instanceBytes);

    // Copy staging -> device-local
    VkBufferCopy cpy{};
    cpy.size = instanceBytes;
    vkCmdCopyBuffer(cmd, t.instanceStaging.buffer(), t.instanceBuffer.buffer(), 1, &cpy);

    // Barrier: transfer write -> AS build read
    VkMemoryBarrier mb0{};
    mb0.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb0.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb0.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         0,
                         1,
                         &mb0,
                         0,
                         nullptr,
                         0,
                         nullptr);

    // Build sizes for TLAS
    VkAccelerationStructureGeometryKHR asGeom{};
    asGeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;

    VkAccelerationStructureGeometryInstancesDataKHR instData{};
    instData.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData.arrayOfPointers    = VK_FALSE;
    instData.data.deviceAddress = vkutil::bufferDeviceAddress(m_ctx.device, t.instanceBuffer.buffer());

    asGeom.geometry.instances = instData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &asGeom;

    const uint32_t primCount = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    m_ctx.rtDispatch->vkGetAccelerationStructureBuildSizesKHR(m_ctx.device,
                                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                              &buildInfo,
                                                              &primCount,
                                                              &sizeInfo);

    if (sizeInfo.accelerationStructureSize == 0 || sizeInfo.buildScratchSize == 0)
        return false;

    // Recreate TLAS buffer/AS if too small or missing
    const bool needNewTlasStorage =
        (!t.buffer.valid() || t.buffer.size() < sizeInfo.accelerationStructureSize || t.as == VK_NULL_HANDLE);

    if (needNewTlasStorage)
    {
        // Destroy old TLAS
        if (t.as != VK_NULL_HANDLE)
        {
            m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, t.as, nullptr);
            t.as = VK_NULL_HANDLE;
        }
        t.buffer.destroy();
        t.address = 0;

        t.buffer.create(m_ctx.device,
                        m_ctx.physicalDevice,
                        sizeInfo.accelerationStructureSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        false,
                        true);

        if (!t.buffer.valid())
            return false;

        VkAccelerationStructureCreateInfoKHR asci{};
        asci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        asci.size   = sizeInfo.accelerationStructureSize;
        asci.buffer = t.buffer.buffer();

        if (m_ctx.rtDispatch->vkCreateAccelerationStructureKHR(m_ctx.device, &asci, nullptr, &t.as) != VK_SUCCESS)
            return false;
    }

    if (!ensureRtScratch(sizeInfo.buildScratchSize))
        return false;

    buildInfo.dstAccelerationStructure  = t.as;
    buildInfo.scratchData.deviceAddress = vkutil::bufferDeviceAddress(m_ctx.device, m_rtScratch.buffer());

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount                                      = primCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRanges[] = {&range};

    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, pRanges);

    // Barrier: TLAS build writes -> RT reads
    VkMemoryBarrier mb1{};
    mb1.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb1.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb1.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         1,
                         &mb1,
                         0,
                         nullptr,
                         0,
                         nullptr);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = t.as;

    t.address  = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    t.buildKey = key;

    return (t.address != 0);
}

//==================================================================
// drawOverlays / drawSelection / drawSceneGrid / ensureOverlayVertexCapacity
// NOTE: Keep these from your current file unchanged.
//==================================================================

void Renderer::drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays)
{
    if (!vp)
        return;

    const auto& lines = overlays.lines();
    if (lines.empty())
        return;

    std::vector<OverlayVertex> vertices;
    vertices.reserve(lines.size() * 2);

    for (const auto& L : lines)
    {
        OverlayVertex v0{};
        v0.pos       = L.p1;
        v0.thickness = L.thickness;
        v0.color     = L.color;
        vertices.push_back(v0);

        OverlayVertex v1{};
        v1.pos       = L.p2;
        v1.thickness = L.thickness;
        v1.color     = L.color;
        vertices.push_back(v1);
    }

    const std::size_t vertexCount = vertices.size();
    if (vertexCount == 0)
        return;

    ensureOverlayVertexCapacity(vertexCount);
    if (!m_overlayVertexBuffer.valid())
        return;

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(vertexCount * sizeof(OverlayVertex));
    m_overlayVertexBuffer.upload(vertices.data(), byteSize);

    if (!m_overlayLinePipeline)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_overlayLinePipeline);

    PushConstants pc{};
    pc.model         = glm::mat4(1.0f);
    pc.color         = glm::vec4(1.0f);
    pc.overlayParams = glm::vec4(static_cast<float>(vp->width()),
                                 static_cast<float>(vp->height()),
                                 1.0f,
                                 0.0f);

    vkCmdPushConstants(cmd,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &pc);

    VkBuffer     vb      = m_overlayVertexBuffer.buffer();
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset0);

    vkCmdDraw(cmd, static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

void Renderer::ensureOverlayVertexCapacity(std::size_t requiredVertexCount)
{
    if (requiredVertexCount == 0)
        return;

    if (requiredVertexCount <= m_overlayVertexCapacity && m_overlayVertexBuffer.valid())
        return;

    if (m_overlayVertexBuffer.valid())
        m_overlayVertexBuffer.destroy();

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(requiredVertexCount * sizeof(OverlayVertex));

    m_overlayVertexBuffer.create(m_ctx.device,
                                 m_ctx.physicalDevice,
                                 bufferSize,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 /*persistentMap*/ true);

    if (!m_overlayVertexBuffer.valid())
    {
        m_overlayVertexCapacity = 0;
        return;
    }

    m_overlayVertexCapacity = requiredVertexCount;
}

void Renderer::drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene)
{
    if (!scene || !vp)
        return;

    if (!m_pipelineSelVert && !m_pipelineSelEdge && !m_pipelineSelPoly)
        return;

    VkDeviceSize zeroOffset = 0;

    const glm::vec4 selColorVisible = glm::vec4(1.0f, 0.55f, 0.10f, 0.6f);
    const glm::vec4 selColorHidden  = glm::vec4(1.0f, 0.55f, 0.10f, 0.3f);

    const bool showOccluded = (vp->drawMode() == DrawMode::WIREFRAME);

    auto pushPC = [&](SceneMesh* sm, const glm::vec4& color) {
        PushConstants pc = {};
        pc.model         = sm->model();
        pc.color         = color;

        vkCmdPushConstants(cmd,
                           m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(PushConstants),
                           &pc);
    };

    auto drawHidden = [&](SceneMesh* sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!showOccluded)
            return;
        if (!pipeline || indexCount == 0)
            return;

        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        pushPC(sm, selColorHidden);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    auto drawVisible = [&](SceneMesh* sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!pipeline || indexCount == 0)
            return;

        vkCmdSetDepthBias(cmd, -1.0f, 0.0f, -1.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        pushPC(sm, selColorVisible);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->visible())
            continue;

        if (!sm->gpu())
            sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

        MeshGpuResources* gpu = sm->gpu();
        gpu->update();

        const bool useSubdiv = (sm->subdivisionLevel() > 0);

        // ---------------------------------------------------------
        // Choose position buffer + selection index buffers
        // ---------------------------------------------------------
        VkBuffer   posVb    = VK_NULL_HANDLE;
        uint32_t   selCount = 0;
        VkBuffer   selIb    = VK_NULL_HANDLE;
        VkPipeline pipeVis  = VK_NULL_HANDLE;
        VkPipeline pipeHid  = VK_NULL_HANDLE;

        const SelectionMode mode = scene->selectionMode();

        if (!useSubdiv)
        {
            if (gpu->uniqueVertCount() == 0 || !gpu->uniqueVertBuffer().valid())
                continue;

            posVb = gpu->uniqueVertBuffer().buffer();

            if (mode == SelectionMode::VERTS)
            {
                if (gpu->selVertIndexCount() == 0 || !gpu->selVertIndexBuffer().valid())
                    continue;

                selCount = gpu->selVertIndexCount();
                selIb    = gpu->selVertIndexBuffer().buffer();
                pipeVis  = m_pipelineSelVert;
                pipeHid  = m_pipelineSelVertHidden;
            }
            else if (mode == SelectionMode::EDGES)
            {
                if (gpu->selEdgeIndexCount() == 0 || !gpu->selEdgeIndexBuffer().valid())
                    continue;

                selCount = gpu->selEdgeIndexCount();
                selIb    = gpu->selEdgeIndexBuffer().buffer();
                pipeVis  = m_pipelineSelEdge;
                pipeHid  = m_pipelineSelEdgeHidden;
            }
            else if (mode == SelectionMode::POLYS)
            {
                if (gpu->selPolyIndexCount() == 0 || !gpu->selPolyIndexBuffer().valid())
                    continue;

                selCount = gpu->selPolyIndexCount();
                selIb    = gpu->selPolyIndexBuffer().buffer();
                pipeVis  = m_pipelineSelPoly;
                pipeHid  = m_pipelineSelPolyHidden;
            }
            else
            {
                continue;
            }
        }
        else
        {
            // Subdiv selection indices are into the subdiv shared vertex buffer.
            if (gpu->subdivSharedVertCount() == 0 || !gpu->subdivSharedVertBuffer().valid())
                continue;

            posVb = gpu->subdivSharedVertBuffer().buffer();

            if (mode == SelectionMode::VERTS)
            {
                if (gpu->subdivSelVertIndexCount() == 0 || !gpu->subdivSelVertIndexBuffer().valid())
                    continue;

                selCount = gpu->subdivSelVertIndexCount();
                selIb    = gpu->subdivSelVertIndexBuffer().buffer();
                pipeVis  = m_pipelineSelVert;
                pipeHid  = m_pipelineSelVertHidden;
            }
            else if (mode == SelectionMode::EDGES)
            {
                if (gpu->subdivSelEdgeIndexCount() == 0 || !gpu->subdivSelEdgeIndexBuffer().valid())
                    continue;

                selCount = gpu->subdivSelEdgeIndexCount();
                selIb    = gpu->subdivSelEdgeIndexBuffer().buffer();
                pipeVis  = m_pipelineSelEdge;
                pipeHid  = m_pipelineSelEdgeHidden;
            }
            else if (mode == SelectionMode::POLYS)
            {
                if (gpu->subdivSelPolyIndexCount() == 0 || !gpu->subdivSelPolyIndexBuffer().valid())
                    continue;

                selCount = gpu->subdivSelPolyIndexCount();
                selIb    = gpu->subdivSelPolyIndexBuffer().buffer();
                pipeVis  = m_pipelineSelPoly;
                pipeHid  = m_pipelineSelPolyHidden;
            }
            else
            {
                continue;
            }
        }

        // ---------------------------------------------------------
        // Bind + draw
        // ---------------------------------------------------------
        vkCmdBindVertexBuffers(cmd, 0, 1, &posVb, &zeroOffset);
        vkCmdBindIndexBuffer(cmd, selIb, 0, VK_INDEX_TYPE_UINT32);

        drawHidden(sm, pipeHid, selCount);
        drawVisible(sm, pipeVis, selCount);
    }

    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
}

void Renderer::drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    if (!scene->showSceneGrid())
        return;

    if (!m_grid)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    // ------------------------------------------------------------
    // Orient the grid depending on the viewport view mode.
    // Grid geometry is authored on XZ (Y=0) as a floor.
    // For FRONT/LEFT/etc we rotate it so it becomes XY or YZ.
    // ------------------------------------------------------------
    glm::mat4 gridModel = glm::mat4(1.0f);

    const float halfPi = glm::half_pi<float>();

    switch (vp->viewMode())
    {
        case ViewMode::TOP:
            // XZ plane (default)
            gridModel = glm::mat4(1.0f);
            break;

        case ViewMode::BOTTOM:
            // still XZ, but flip it
            gridModel = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            break;

        case ViewMode::FRONT:
            // want XY plane -> rotate XZ around +X by -90°
            gridModel = glm::rotate(glm::mat4(1.0f), -halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            break;

        case ViewMode::BACK:
            // XY plane, opposite
            gridModel = glm::rotate(glm::mat4(1.0f), +halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            break;

        case ViewMode::LEFT:
            // want YZ plane -> rotate XZ around +Z by +90°
            gridModel = glm::rotate(glm::mat4(1.0f), +halfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            break;

        case ViewMode::RIGHT:
            // YZ plane, opposite
            gridModel = glm::rotate(glm::mat4(1.0f), -halfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            break;

        default:
            // Perspective / other: treat as floor grid
            gridModel = glm::mat4(1.0f);
            break;
    }

    PushConstants pc{};
    pc.model = gridModel;
    pc.color = glm::vec4(0, 0, 0, 0);

    vkCmdPushConstants(cmd,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &pc);

    m_grid->render(cmd);
}

#if 0
//============================================================
// Renderer.cpp
//============================================================
#include "Renderer.hpp"

#include <Sysmesh.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <glm/vec3.hpp>
#include <iostream>
#include <vector>

#include "GpuResources/GpuMaterial.hpp"
#include "GpuResources/MeshGpuResources.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "GridRendererVK.hpp"
#include "MeshGpuResources.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "ShaderStage.hpp"
#include "Viewport.hpp"
#include "VkPipelineHelpers.hpp"
#include "VkUtilities.hpp"

namespace
{
    static void imageBarrier(VkCommandBuffer      cmd,
                             VkImage              image,
                             VkImageLayout        oldLayout,
                             VkImageLayout        newLayout,
                             VkAccessFlags        srcAccess,
                             VkAccessFlags        dstAccess,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b{};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                       = oldLayout;
        b.newLayout                       = newLayout;
        b.srcAccessMask                   = srcAccess;
        b.dstAccessMask                   = dstAccess;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = image;
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd,
                             srcStage,
                             dstStage,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &b);
    }

    static void writeTlasDescriptor(VkDevice device, VkDescriptorSet set, VkAccelerationStructureKHR tlas)
    {
        VkWriteDescriptorSetAccelerationStructureKHR asInfo = {};
        asInfo.sType                                        = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount                   = 1;
        asInfo.pAccelerationStructures                      = &tlas;

        VkWriteDescriptorSet write = {};
        write.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext                = &asInfo;
        write.dstSet               = set;
        write.dstBinding           = 3; // binding 3 in the RT set layout
        write.dstArrayElement      = 0;
        write.descriptorCount      = 1;
        write.descriptorType       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // static VkClearColorValue toVkClear(const glm::vec4& c) noexcept
    // {
    //     VkClearColorValue v{};
    //     v.float32[0] = c.r;
    //     v.float32[1] = c.g;
    //     v.float32[2] = c.b;
    //     v.float32[3] = c.a;
    //     return v;
    // }

    // ============================================================
    // DEBUG: force rebuild BLAS + TLAS every frame
    // ============================================================
    constexpr bool kRtRebuildAsEveryFrame = false;

} // namespace

//==================================================================
// Init / Lifetime
//==================================================================
Renderer::Renderer() noexcept : m_rtTlasChangeCounter{std::make_shared<SysCounter>()}, m_rtTlasChangeMonitor{m_rtTlasChangeCounter}
{
}

bool Renderer::initDevice(const VulkanContext& ctx)
{
    m_ctx            = ctx;
    m_framesInFlight = std::max(1u, m_ctx.framesInFlight);

    // set=0 MVP resources are now per-viewport, allocated lazily in ensureViewportUboState().
    m_viewportUbos.clear();

    m_rtTlasFrames.clear();
    m_rtTlasFrames.resize(m_framesInFlight);

    // Descriptors (layouts + pool + per-frame sets)
    if (!createDescriptors(m_framesInFlight))
        return false;

    // Pipeline layout depends on descriptor set layouts + push constant range.
    if (!createPipelineLayout())
        return false;

    // Optional: device-level grid resources (vertex buffer, etc.)
    m_grid = std::make_unique<GridRendererVK>(&m_ctx);
    m_grid->createDeviceResources();

    if (rtReady(m_ctx))
    {
        if (!initRayTracingResources())
        {
            std::cerr << "initRayTracingResources() failed." << std::endl;
            return false;
        }
    }

    return true;
}

bool Renderer::initSwapchain(VkRenderPass renderPass)
{
    // Pipelines are render-pass dependent → always rebuild on swapchain changes.
    destroyPipelines();

    if (!createPipelines(renderPass))
        return false;

    if (m_grid && !m_grid->createPipeline(renderPass, m_pipelineLayout))
        return false;

    // ------------------------------------------------------------
    // RT present pipeline (fullscreen) is swapchain/render-pass dependent
    // ------------------------------------------------------------
    if (rtReady(m_ctx))
    {
        if (!createRtPresentPipeline(renderPass))
        {
            std::cerr << "createRtPresentPipeline() failed." << std::endl;
            return false;
        }
    }

    return true;
}

void Renderer::destroySwapchainResources() noexcept
{
    if (m_grid)
        m_grid->destroySwapchainResources();

    // RT present pipeline is swapchain/render-pass dependent.
    destroyRtPresentPipeline();

    destroyPipelines();
}

void Renderer::shutdown() noexcept
{
    destroySwapchainResources();

    // Destroy per-viewport MVP state (device-level).
    for (auto& [vp, state] : m_viewportUbos)
    {
        for (auto& buf : state.mvpBuffers)
            buf.destroy();
        state.mvpBuffers.clear();
        state.uboSets.clear();
    }
    m_viewportUbos.clear();

    m_materialBuffer.destroy();
    m_materialSets.clear();

    m_descriptorPool.destroy();
    m_descriptorSetLayout.destroy();
    m_materialSetLayout.destroy();

    // ------------------------------------------------------------
    // RT device-level resources
    // ------------------------------------------------------------
    destroyAllRtTlasFrames();
    destroyAllRtBlas();

    for (auto& buf : m_rtCameraBuffers)
        buf.destroy();
    m_rtCameraBuffers.clear();

    destroyRtOutputImages();

    m_rtSbt.destroy();
    m_rtPipeline.destroy();

    m_rtScratch.destroy();
    m_rtScratchSize = 0;

    m_rtSets.clear();
    m_rtPool.destroy();
    m_rtSetLayout.destroy();

    if (m_rtSampler != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroySampler(m_ctx.device, m_rtSampler, nullptr);
        m_rtSampler = VK_NULL_HANDLE;
    }

    if (m_rtUploadPool != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyCommandPool(m_ctx.device, m_rtUploadPool, nullptr);
        m_rtUploadPool = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_grid)
        m_grid->destroyDeviceResources();
    m_grid.reset();

    m_overlayVertexBuffer.destroy();
    m_overlayVertexCapacity = 0;

    m_materialCount      = 0;
    m_curMaterialCounter = 0;
    m_framesInFlight     = 0;
    m_ctx                = {};

    m_rtTlasLinkedMeshes.clear();
    m_rtTlasChangeCounter.reset();
}

void Renderer::idle(Scene* scene)
{
    if (!scene)
        return;

    // Link all meshes once: topo/deform -> renderer TLAS counter
    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm)
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        if (m_rtTlasLinkedMeshes.contains(mesh))
            continue;

        mesh->topology_counter()->addParent(m_rtTlasChangeCounter);
        mesh->deform_counter()->addParent(m_rtTlasChangeCounter);

        m_rtTlasLinkedMeshes.insert(mesh);
    }

    // If anything changed, force TLAS rebuild next ensureSceneTlas()
    if (m_rtTlasChangeMonitor.changed())
    {
        for (RtTlasFrame& tf : m_rtTlasFrames)
            tf.buildKey = 0;
    }
}

void Renderer::waitDeviceIdle() noexcept
{
    if (!m_ctx.device)
        return;

    vkDeviceWaitIdle(m_ctx.device);
}

//==================================================================
// Pipeline destruction (swapchain-level)
//==================================================================

void Renderer::destroyPipelines() noexcept
{
    if (!m_ctx.device)
        return;

    VkDevice device = m_ctx.device;

    // NOTE:
    // This is a heavy hammer. It guarantees no in-flight use of these pipelines.
    // Your *frame fences* already guarantee safety during normal frame-to-frame use,
    // but during swapchain rebuild this is fine.
    vkDeviceWaitIdle(device);

    auto destroyPipe = [&](VkPipeline& p) noexcept {
        if (p)
        {
            vkDestroyPipeline(device, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    };

    destroyPipe(m_pipelineSolid);
    destroyPipe(m_pipelineShaded);
    destroyPipe(m_pipelineDepthOnly);
    destroyPipe(m_pipelineWire);
    destroyPipe(m_pipelineEdgeHidden);
    destroyPipe(m_pipelineEdgeDepthBias);
    destroyPipe(m_overlayLinePipeline);

    destroyPipe(m_pipelineSelVert);
    destroyPipe(m_pipelineSelEdge);
    destroyPipe(m_pipelineSelPoly);
    destroyPipe(m_pipelineSelVertHidden);
    destroyPipe(m_pipelineSelEdgeHidden);
    destroyPipe(m_pipelineSelPolyHidden);
}

//==================================================================
// Descriptors + pipeline layout (device-level)
//==================================================================

bool Renderer::createDescriptors(uint32_t framesInFlight)
{
    VkDevice device = m_ctx.device;

    // set=0 binding=0 : MVP UBO (VS + GS)
    DescriptorBindingInfo uboBinding{};
    uboBinding.binding = 0;
    uboBinding.type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.stages  = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
    uboBinding.count   = 1;

    if (!m_descriptorSetLayout.create(device, std::span{&uboBinding, 1}))
    {
        std::cerr << "RendererVK: Failed to create UBO DescriptorSetLayout.\n";
        return false;
    }

    // set=1 binding=0 : materials SSBO (FS)
    // set=1 binding=1 : texture table sampler array (FS)
    DescriptorBindingInfo matBindings[2]{};

    matBindings[0].binding = 0;
    matBindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    matBindings[0].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    matBindings[0].count   = 1;

    matBindings[1].binding = 1;
    matBindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    matBindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    matBindings[1].count   = kMaxTextureCount;

    if (!m_materialSetLayout.create(device, std::span{matBindings, 2}))
    {
        std::cerr << "RendererVK: Failed to create material DescriptorSetLayout.\n";
        return false;
    }

    // IMPORTANT:
    // set=0 (MVP UBO) is allocated per-viewport-per-frame (lazy). We must size the pool to handle
    // multiple viewports without re-creating the pool.
    constexpr uint32_t kMaxViewports = 8;

    std::array<VkDescriptorPoolSize, 3> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, framesInFlight * kMaxViewports},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, framesInFlight * kMaxTextureCount},
    };

    const uint32_t maxSets = (framesInFlight * kMaxViewports) + framesInFlight;

    if (!m_descriptorPool.create(device, poolSizes, maxSets))
    {
        std::cerr << "RendererVK: Failed to create shared DescriptorPool.\n";
        return false;
    }

    m_materialSets.clear();
    m_materialSets.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        if (!m_materialSets[i].allocate(device, m_descriptorPool.pool(), m_materialSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate material DescriptorSet for frame " << i << ".\n";
            return false;
        }
    }

    return true;
}

bool Renderer::createPipelineLayout() noexcept
{
    if (!m_ctx.device)
        return false;

    if (m_pipelineLayout != VK_NULL_HANDLE)
        return true;

    VkDescriptorSetLayout setLayouts[2] = {
        m_descriptorSetLayout.layout(), // set 0
        m_materialSetLayout.layout(),   // set 1
    };

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                         VK_SHADER_STAGE_GEOMETRY_BIT |
                         VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size   = sizeof(PushConstants);

    m_pipelineLayout = vkutil::createPipelineLayout(m_ctx.device, setLayouts, 2, &pcRange, 1);
    return (m_pipelineLayout != VK_NULL_HANDLE);
}

//==================================================================
// Per-viewport MVP UBO (device-level)
//==================================================================

Renderer::ViewportUboState& Renderer::ensureViewportUboState(Viewport* vp)
{
    auto it = m_viewportUbos.find(vp);
    if (it != m_viewportUbos.end())
        return it->second;

    ViewportUboState state{};
    state.mvpBuffers.resize(m_framesInFlight);
    state.uboSets.resize(m_framesInFlight);

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        state.mvpBuffers[i].create(m_ctx.device,
                                   m_ctx.physicalDevice,
                                   sizeof(MvpUBO),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   /*persistentMap*/ true);

        if (!state.mvpBuffers[i].valid())
        {
            std::cerr << "RendererVK: Failed to create MVP uniform buffer for viewport at frame " << i << std::endl;
            break;
        }

        if (!state.uboSets[i].allocate(m_ctx.device, m_descriptorPool.pool(), m_descriptorSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate UBO DescriptorSet for viewport at frame " << i << std::endl;
            break;
        }

        state.uboSets[i].writeUniformBuffer(m_ctx.device, 0, state.mvpBuffers[i].buffer(), sizeof(MvpUBO));
    }

    auto [insIt, _] = m_viewportUbos.emplace(vp, std::move(state));
    return insIt->second;
}

//==================================================================
// RT geometry selection
//==================================================================

Renderer::RtMeshGeometry Renderer::selectRtGeometry(SceneMesh* sm) noexcept
{
    RtMeshGeometry out = {};

    if (!sm || !sm->gpu())
        return out;

    MeshGpuResources* gpu = sm->gpu();
    if (!gpu)
        return out;

    const bool useSubdiv = (sm->subdivisionLevel() > 0);

    if (!useSubdiv)
    {
        // BLAS build inputs (coarse): unique verts + coarse tri indices
        if (gpu->uniqueVertCount() == 0 || !gpu->uniqueVertBuffer().valid())
            return out;

        if (gpu->coarseTriIndexCount() == 0 || !gpu->coarseTriIndexBuffer().valid())
            return out;

        // Shading inputs (coarse): corner-expanded RT streams
        if (gpu->coarseRtPosCount() == 0 || !gpu->coarseRtPosBuffer().valid())
            return out;

        if (gpu->coarseRtCornerNrmCount() == 0 || !gpu->coarseRtCornerNrmBuffer().valid())
            return out;

        if (gpu->coarseRtCornerUvCount() == 0 || !gpu->coarseRtCornerUvBuffer().valid())
            return out;

        // Shader index buffer (coarse): uvec4-per-tri padded buffer (or equivalent)
        if (gpu->coarseRtTriCount() == 0 || !gpu->coarseRtTriIndexBuffer().valid())
            return out;

        out.buildPosBuffer = gpu->uniqueVertBuffer().buffer();
        out.buildPosCount  = gpu->uniqueVertCount();

        out.buildIndexBuffer = gpu->coarseTriIndexBuffer().buffer();
        out.buildIndexCount  = gpu->coarseTriIndexCount();

        out.shadePosBuffer = gpu->coarseRtPosBuffer().buffer();
        out.shadePosCount  = gpu->coarseRtPosCount();

        out.shadeNrmBuffer = gpu->coarseRtCornerNrmBuffer().buffer();
        out.shadeNrmCount  = gpu->coarseRtCornerNrmCount();

        out.shadeUvBuffer = gpu->coarseRtCornerUvBuffer().buffer();
        out.shadeUvCount  = gpu->coarseRtCornerUvCount();

        out.shaderIndexBuffer = gpu->coarseRtTriIndexBuffer().buffer();
        out.shaderTriCount    = gpu->coarseRtTriCount();

        return out;
    }
    else
    {
        // BLAS build inputs (subdiv): subdiv shared verts + subdiv shared tri indices
        if (gpu->subdivSharedVertCount() == 0 || !gpu->subdivSharedVertBuffer().valid())
            return out;

        if (gpu->subdivSharedTriIndexCount() == 0 || !gpu->subdivSharedTriIndexBuffer().valid())
            return out;

        // Shading inputs (subdiv): corner-expanded RT streams
        if (gpu->subdivRtPosCount() == 0 || !gpu->subdivRtPosBuffer().valid())
            return out;

        if (gpu->subdivRtCornerNrmCount() == 0 || !gpu->subdivRtCornerNrmBuffer().valid())
            return out;

        if (gpu->subdivRtCornerUvCount() == 0 || !gpu->subdivRtCornerUvBuffer().valid())
            return out;

        // Shader index buffer (subdiv): uvec4-per-tri padded buffer (or equivalent)
        if (gpu->subdivRtTriCount() == 0 || !gpu->subdivRtTriIndexBuffer().valid())
            return out;

        out.buildPosBuffer = gpu->subdivSharedVertBuffer().buffer();
        out.buildPosCount  = gpu->subdivSharedVertCount();

        out.buildIndexBuffer = gpu->subdivSharedTriIndexBuffer().buffer();
        out.buildIndexCount  = gpu->subdivSharedTriIndexCount();

        out.shadePosBuffer = gpu->subdivRtPosBuffer().buffer();
        out.shadePosCount  = gpu->subdivRtPosCount();

        out.shadeNrmBuffer = gpu->subdivRtCornerNrmBuffer().buffer();
        out.shadeNrmCount  = gpu->subdivRtCornerNrmCount();

        out.shadeUvBuffer = gpu->subdivRtCornerUvBuffer().buffer();
        out.shadeUvCount  = gpu->subdivRtCornerUvCount();

        out.shaderIndexBuffer = gpu->subdivRtTriIndexBuffer().buffer();
        out.shaderTriCount    = gpu->subdivRtTriCount();

        return out;
    }
}

//==================================================================
// Materials
//==================================================================

void Renderer::uploadMaterialsToGpu(const std::vector<Material>& materials,
                                    TextureHandler&              texHandler,
                                    uint32_t                     frameIndex)
{
    if (frameIndex >= m_framesInFlight)
        return;

    m_materialCount = static_cast<std::uint32_t>(materials.size());
    if (m_materialCount == 0)
        return;

    std::vector<GpuMaterial> gpuMats;
    buildGpuMaterialArray(materials, texHandler, gpuMats);

    const VkDeviceSize sizeBytes = static_cast<VkDeviceSize>(gpuMats.size() * sizeof(GpuMaterial));

    if (!m_materialBuffer.valid() || m_materialBuffer.size() < sizeBytes)
    {
        m_materialBuffer.destroy();
        m_materialBuffer.create(m_ctx.device,
                                m_ctx.physicalDevice,
                                sizeBytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                /*persistentMap*/ false);
    }

    m_materialBuffer.upload(gpuMats.data(), sizeBytes);

    m_materialSets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                  0,
                                                  m_materialBuffer.buffer(),
                                                  sizeBytes,
                                                  0);
}

void Renderer::updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex)
{
    if (frameIndex >= m_framesInFlight)
        return;

    VkDevice device = m_ctx.device;
    if (!device)
        return;

    const int texCount = static_cast<int>(textureHandler.size());
    const int count    = std::min(texCount, static_cast<int>(kMaxTextureCount));

    if (count <= 0)
        return;

    std::vector<VkDescriptorImageInfo> infos;
    infos.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const GpuTexture* tex = textureHandler.get(i);

        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (tex)
        {
            info.imageView = tex->view;
            info.sampler   = tex->sampler;
        }
        else
        {
            info.imageView = VK_NULL_HANDLE;
            info.sampler   = VK_NULL_HANDLE;
        }

        infos.push_back(info);
    }

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_materialSets[frameIndex].set();
    write.dstBinding      = 1;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = static_cast<uint32_t>(infos.size());
    write.pImageInfo      = infos.data();

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

//==================================================================
// Pipelines (swapchain-level)
//==================================================================
// (UNCHANGED BELOW HERE until RT present pipeline)
//==================================================================

bool Renderer::createPipelines(VkRenderPass renderPass)
{
    if (m_pipelineLayout == VK_NULL_HANDLE)
    {
        std::cerr << "RendererVK: createPipelines called before pipeline layout was created.\n";
        return false;
    }

    const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

    ShaderStage SolidDrawVert = vkutil::loadStage(m_ctx.device, shaderDir, "SolidDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage SolidDrawFrag = vkutil::loadStage(m_ctx.device, shaderDir, "SolidDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    ShaderStage ShadedDrawVert = vkutil::loadStage(m_ctx.device, shaderDir, "ShadedDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage ShadedDrawFrag = vkutil::loadStage(m_ctx.device, shaderDir, "ShadedDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    ShaderStage wireVert          = vkutil::loadStage(m_ctx.device, shaderDir, "Wireframe.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage wireFrag          = vkutil::loadStage(m_ctx.device, shaderDir, "Wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    ShaderStage wireDepthBiasVert = vkutil::loadStage(m_ctx.device, shaderDir, "WireframeDepthBias.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);

    ShaderStage overlayVert = vkutil::loadStage(m_ctx.device, shaderDir, "Overlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage overlayGeom = vkutil::loadStage(m_ctx.device, shaderDir, "Overlay.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);
    ShaderStage overlayFrag = vkutil::loadStage(m_ctx.device, shaderDir, "Overlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    ShaderStage selVert     = vkutil::loadStage(m_ctx.device, shaderDir, "Selection.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage selFrag     = vkutil::loadStage(m_ctx.device, shaderDir, "Selection.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    ShaderStage selVertFrag = vkutil::loadStage(m_ctx.device, shaderDir, "SelectionVert.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!SolidDrawVert.isValid() || !SolidDrawFrag.isValid() ||
        !ShadedDrawVert.isValid() || !ShadedDrawFrag.isValid() ||
        !wireVert.isValid() || !wireFrag.isValid() || !wireDepthBiasVert.isValid() ||
        !overlayVert.isValid() || !overlayGeom.isValid() || !overlayFrag.isValid() ||
        !selVert.isValid() || !selFrag.isValid() || !selVertFrag.isValid())
    {
        std::cerr << "RendererVK: Failed to load one or more shader modules.\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo SolidDrawStages[2]  = {SolidDrawVert.stageInfo(), SolidDrawFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo ShadedDrawStages[2] = {ShadedDrawVert.stageInfo(), ShadedDrawFrag.stageInfo()};

    VkPipelineShaderStageCreateInfo wireStages[2]          = {wireVert.stageInfo(), wireFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo wireDepthBiasStages[2] = {wireDepthBiasVert.stageInfo(), wireFrag.stageInfo()};

    VkPipelineShaderStageCreateInfo overlayStages[3] = {overlayVert.stageInfo(), overlayGeom.stageInfo(), overlayFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo selStages[2]     = {selVert.stageInfo(), selFrag.stageInfo()};
    VkPipelineShaderStageCreateInfo selVertStages[2] = {selVert.stageInfo(), selVertFrag.stageInfo()};

    VkVertexInputBindingDescription      solidBindings[4]{};
    VkVertexInputAttributeDescription    solidAttrs[4]{};
    VkPipelineVertexInputStateCreateInfo viSolid{};
    vkutil::makeSolidVertexInput(viSolid, solidBindings, solidAttrs);

    VkVertexInputBindingDescription      lineBinding{};
    VkVertexInputAttributeDescription    lineAttr{};
    VkPipelineVertexInputStateCreateInfo viLines{};
    vkutil::makeLineVertexInput(viLines, lineBinding, lineAttr);

    VkVertexInputBindingDescription overlayBinding{};
    overlayBinding.binding   = 0;
    overlayBinding.stride    = sizeof(OverlayVertex);
    overlayBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription overlayAttrs[3]{};

    overlayAttrs[0].location = 0;
    overlayAttrs[0].binding  = 0;
    overlayAttrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    overlayAttrs[0].offset   = offsetof(OverlayVertex, pos);

    overlayAttrs[1].location = 1;
    overlayAttrs[1].binding  = 0;
    overlayAttrs[1].format   = VK_FORMAT_R32_SFLOAT;
    overlayAttrs[1].offset   = offsetof(OverlayVertex, thickness);

    overlayAttrs[2].location = 2;
    overlayAttrs[2].binding  = 0;
    overlayAttrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    overlayAttrs[2].offset   = offsetof(OverlayVertex, color);

    VkPipelineVertexInputStateCreateInfo viOverlay{};
    viOverlay.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viOverlay.vertexBindingDescriptionCount   = 1;
    viOverlay.pVertexBindingDescriptions      = &overlayBinding;
    viOverlay.vertexAttributeDescriptionCount = 3;
    viOverlay.pVertexAttributeDescriptions    = overlayAttrs;

    vkutil::MeshPipelinePreset solidPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = true,
        .depthCompareOp      = VK_COMPARE_OP_LESS,
        .enableBlend         = false,
        .enableDepthBias     = false,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset wirePreset{
        .topology              = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode           = VK_POLYGON_MODE_FILL,
        .cullMode              = VK_CULL_MODE_NONE,
        .frontFace             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest             = true,
        .depthWrite            = false,
        .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend           = true,
        .enableDepthBias       = false,
        .colorWrite            = true,
        .sampleShadingEnable   = false,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = false,
    };

    vkutil::MeshPipelinePreset edgeOverlayPreset = wirePreset;

    vkutil::MeshPipelinePreset depthOnlyPreset = solidPreset;
    depthOnlyPreset.enableBlend                = false;
    depthOnlyPreset.depthWrite                 = true;
    depthOnlyPreset.depthCompareOp             = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthOnlyPreset.colorWrite                 = false;

    vkutil::MeshPipelinePreset hiddenEdgePreset = wirePreset;
    hiddenEdgePreset.depthCompareOp             = VK_COMPARE_OP_GREATER;
    hiddenEdgePreset.depthWrite                 = false;

    vkutil::MeshPipelinePreset overlayPreset{
        .topology              = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode           = VK_POLYGON_MODE_FILL,
        .cullMode              = VK_CULL_MODE_NONE,
        .frontFace             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest             = false,
        .depthWrite            = false,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .enableBlend           = true,
        .enableDepthBias       = false,
        .colorWrite            = true,
        .sampleShadingEnable   = false,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = false,
    };

    vkutil::MeshPipelinePreset selVertPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selEdgePreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selPolyPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selVertHiddenPreset = selVertPreset;
    selVertHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    vkutil::MeshPipelinePreset selEdgeHiddenPreset = selEdgePreset;
    selEdgeHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    vkutil::MeshPipelinePreset selPolyHiddenPreset = selPolyPreset;
    selPolyHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    m_pipelineSolid = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, SolidDrawStages, 2, &viSolid, solidPreset);
    if (!m_pipelineSolid)
    {
        std::cerr << "RendererVK: createMeshPipeline(solid) failed.\n";
        return false;
    }

    m_pipelineShaded = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, ShadedDrawStages, 2, &viSolid, solidPreset);
    if (!m_pipelineShaded)
    {
        std::cerr << "RendererVK: createMeshPipeline(shaded) failed.\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo depthStages[1] = {SolidDrawVert.stageInfo()};
    m_pipelineDepthOnly                            = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, depthStages, 1, &viSolid, depthOnlyPreset);
    if (!m_pipelineDepthOnly)
    {
        std::cerr << "RendererVK: createMeshPipeline(depthOnly) failed.\n";
        return false;
    }

    m_pipelineWire = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, wireStages, 2, &viLines, wirePreset);
    if (!m_pipelineWire)
    {
        std::cerr << "RendererVK: createMeshPipeline(wire) failed.\n";
        return false;
    }

    m_pipelineEdgeHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, wireStages, 2, &viLines, hiddenEdgePreset);
    if (!m_pipelineEdgeHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(edgeHidden) failed.\n";
        return false;
    }

    m_pipelineEdgeDepthBias = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, wireDepthBiasStages, 2, &viLines, edgeOverlayPreset);
    if (!m_pipelineEdgeDepthBias)
    {
        std::cerr << "RendererVK: createMeshPipeline(edgeOverlay) failed.\n";
        return false;
    }

    m_overlayLinePipeline = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, overlayStages, 3, &viOverlay, overlayPreset);
    if (!m_overlayLinePipeline)
    {
        std::cerr << "RendererVK: createMeshPipeline(overlay) failed.\n";
        return false;
    }

    m_pipelineSelVert = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selVertStages, 2, &viLines, selVertPreset);
    if (!m_pipelineSelVert)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection verts) failed.\n";
        return false;
    }

    m_pipelineSelEdge = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selEdgePreset);
    if (!m_pipelineSelEdge)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection edges) failed.\n";
        return false;
    }

    m_pipelineSelPoly = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selPolyPreset);
    if (!m_pipelineSelPoly)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection polys) failed.\n";
        return false;
    }

    m_pipelineSelVertHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selVertStages, 2, &viLines, selVertHiddenPreset);
    if (!m_pipelineSelVertHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection verts hidden) failed.\n";
        return false;
    }

    m_pipelineSelEdgeHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selEdgeHiddenPreset);
    if (!m_pipelineSelEdgeHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection edges hidden) failed.\n";
        return false;
    }

    m_pipelineSelPolyHidden = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selPolyHiddenPreset);
    if (!m_pipelineSelPolyHidden)
    {
        std::cerr << "RendererVK: createMeshPipeline(selection polys hidden) failed.\n";
        return false;
    }

    return true;
}

//==================================================================
// Step 6: RT present pipeline + RT output images (PER-FRAME)
//==================================================================

bool Renderer::createRtPresentPipeline(VkRenderPass renderPass)
{
    destroyRtPresentPipeline();

    if (!rtReady(m_ctx))
        return true;

    if (!m_rtSetLayout.layout())
    {
        std::cerr << "RendererVK: RT set layout not created yet.\n";
        return false;
    }

    const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

    ShaderStage vs = vkutil::loadStage(m_ctx.device, shaderDir, "RtPresent.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage fs = vkutil::loadStage(m_ctx.device, shaderDir, "RtPresent.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!vs.isValid() || !fs.isValid())
    {
        std::cerr << "RendererVK: Failed to load RtPresent shaders.\n";
        return false;
    }

    VkDescriptorSetLayout setLayouts[1] = {m_rtSetLayout.layout()};

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = setLayouts;

    plci.pushConstantRangeCount = 0;
    plci.pPushConstantRanges    = nullptr;

    if (vkCreatePipelineLayout(m_ctx.device, &plci, nullptr, &m_rtPresentLayout) != VK_SUCCESS)
    {
        std::cerr << "RendererVK: vkCreatePipelineLayout(RtPresent) failed.\n";
        destroyRtPresentPipeline();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {vs.stageInfo(), fs.stageInfo()};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = m_ctx.sampleCount;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                           VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cbAtt;

    VkDynamicState                   dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &dyn;
    gp.layout              = m_rtPresentLayout;
    gp.renderPass          = renderPass;
    gp.subpass             = 0;

    if (vkCreateGraphicsPipelines(m_ctx.device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_rtPresentPipeline) != VK_SUCCESS)
    {
        std::cerr << "RendererVK: vkCreateGraphicsPipelines(RtPresent) failed.\n";
        destroyRtPresentPipeline();
        return false;
    }

    return true;
}

//==================================================================
// RT output images (PER-FRAME)
//==================================================================

void Renderer::destroyRtOutputImages() noexcept
{
    if (!m_ctx.device)
        return;

    VkDevice device = m_ctx.device;

    for (RtImagePerFrame& img : m_rtImages)
    {
        if (img.view)
        {
            vkDestroyImageView(device, img.view, nullptr);
            img.view = VK_NULL_HANDLE;
        }

        if (img.image)
        {
            vkDestroyImage(device, img.image, nullptr);
            img.image = VK_NULL_HANDLE;
        }

        if (img.memory)
        {
            vkFreeMemory(device, img.memory, nullptr);
            img.memory = VK_NULL_HANDLE;
        }

        img.width     = 0;
        img.height    = 0;
        img.needsInit = true;
    }

    m_rtImages.clear();
}

bool Renderer::ensureRtOutputImages(uint32_t w, uint32_t h)
{
    if (!rtReady(m_ctx))
        return false;

    if (w == 0 || h == 0)
        return false;

    if (m_rtSets.empty())
        return false;

    const uint32_t frames = static_cast<uint32_t>(m_rtSets.size());
    if (frames == 0)
        return false;

    if (m_rtImages.size() != frames)
        m_rtImages.resize(frames);

    // Fast path: all images already match size
    bool allOk = true;
    for (uint32_t i = 0; i < frames; ++i)
    {
        const RtImagePerFrame& img = m_rtImages[i];
        if (!img.image || !img.view || img.width != w || img.height != h)
        {
            allOk = false;
            break;
        }
    }
    if (allOk)
        return true;

    // Recreate all per-frame images for simplicity (safe + predictable).
    destroyRtOutputImages();
    m_rtImages.resize(frames);

    VkDevice device = m_ctx.device;

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_ctx.physicalDevice, &memProps);

    auto findDeviceLocalType = [&](uint32_t typeBits) noexcept -> uint32_t {
        for (uint32_t m = 0; m < memProps.memoryTypeCount; ++m)
        {
            if ((typeBits & (1u << m)) &&
                (memProps.memoryTypes[m].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                return m;
            }
        }
        return UINT32_MAX;
    };

    for (uint32_t i = 0; i < frames; ++i)
    {
        RtImagePerFrame img = {};

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType   = VK_IMAGE_TYPE_2D;
        ici.format      = m_rtFormat;
        ici.extent      = VkExtent3D{w, h, 1};
        ici.mipLevels   = 1;
        ici.arrayLayers = 1;
        ici.samples     = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling      = VK_IMAGE_TILING_OPTIMAL;

        // IMPORTANT:
        //  - STORAGE: raygen writes (imageStore)
        //  - SAMPLED: RtPresent.frag samples
        //  - TRANSFER_DST: vkCmdClearColorImage
        ici.usage =
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device, &ici, nullptr, &img.image) != VK_SUCCESS)
            return false;

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, img.image, &req);

        const uint32_t typeIndex = findDeviceLocalType(req.memoryTypeBits);
        if (typeIndex == UINT32_MAX)
            return false;

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = typeIndex;

        // Optional but harmless: keeps you consistent with other device-local allocations.
        // (Doesn't change anything unless you later query device addresses for this memory.)
        VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flagsInfo.flags = 0; // no device-address needed for images
        mai.pNext       = &flagsInfo;

        if (vkAllocateMemory(device, &mai, nullptr, &img.memory) != VK_SUCCESS)
            return false;

        if (vkBindImageMemory(device, img.image, img.memory, 0) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image                           = img.image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = m_rtFormat;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &vci, nullptr, &img.view) != VK_SUCCESS)
            return false;

        img.width     = w;
        img.height    = h;
        img.needsInit = true;

        m_rtImages[i] = img;

        // Update RT descriptor set for THIS frame:
        // - binding 0: storage image (raygen writes; layout at dispatch time: GENERAL)
        // - binding 1: combined sampler (present samples; layout at sample time: SHADER_READ_ONLY_OPTIMAL)
        m_rtSets[i].writeStorageImage(m_ctx.device, 0, m_rtImages[i].view, VK_IMAGE_LAYOUT_GENERAL);

        m_rtSets[i].writeCombinedImageSampler(m_ctx.device,
                                              1,
                                              m_rtSampler,
                                              m_rtImages[i].view,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    return true;
}

//==================================================================
// RT init (device-level)
//==================================================================

bool Renderer::initRayTracingResources()
{
    if (!rtReady(m_ctx))
        return false;

    VkDevice device = m_ctx.device;
    if (!device)
        return false;

    // ------------------------------------------------------------
    // RT descriptor set layout: set=0
    //  binding 0: storage image (raygen writes)
    //  binding 1: combined sampler (present pass samples)
    //  binding 2: RT camera UBO (raygen reads)
    //  binding 3: TLAS (raygen/closestHit reads)
    // ------------------------------------------------------------
    DescriptorBindingInfo bindings[5] = {};

    bindings[0].binding = 0;
    bindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[0].count   = 1;

    bindings[1].binding = 1;
    bindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].count   = 1;

    bindings[2].binding = 2;
    bindings[2].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[2].count   = 1;

    bindings[3].binding = 3;
    bindings[3].type    = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[3].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[3].count   = 1;

    // binding 4: instance data SSBO (hit/closest-hit reads)
    bindings[4].binding = 4;
    bindings[4].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[4].count   = 1;

    if (!m_rtSetLayout.create(device, std::span{bindings, 5}))
    {
        std::cerr << "RendererVK: Failed to create RT DescriptorSetLayout.\n";
        return false;
    }

    // Pool: per-frame sets
    const uint32_t frames = std::max(1u, m_framesInFlight);

    std::array<VkDescriptorPoolSize, 5> poolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frames},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frames},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frames},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, frames},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frames},
    };

    if (!m_rtPool.create(device, poolSizes, /*maxSets*/ frames))
    {
        std::cerr << "RendererVK: Failed to create RT DescriptorPool.\n";
        return false;
    }

    // Allocate per-frame RT sets
    m_rtSets.clear();
    m_rtSets.resize(frames);

    for (uint32_t i = 0; i < frames; ++i)
    {
        if (!m_rtSets[i].allocate(device, m_rtPool.pool(), m_rtSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate RT DescriptorSet for frame " << i << ".\n";
            return false;
        }
    }

    // RT camera buffers (one per frame)
    m_rtCameraBuffers.clear();
    m_rtCameraBuffers.resize(frames);

    for (uint32_t i = 0; i < frames; ++i)
    {
        m_rtCameraBuffers[i].create(device,
                                    m_ctx.physicalDevice,
                                    sizeof(RtCameraUBO),
                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    /*persistentMap*/ true);

        if (!m_rtCameraBuffers[i].valid())
        {
            std::cerr << "RendererVK: Failed to create RT camera UBO for frame " << i << ".\n";
            return false;
        }

        m_rtSets[i].writeUniformBuffer(device, 2, m_rtCameraBuffers[i].buffer(), sizeof(RtCameraUBO));
    }

    // RT instance data
    m_rtInstanceDataBuffers.clear();
    m_rtInstanceDataBuffers.resize(m_framesInFlight);

    for (uint32_t i = 0; i < frames; ++i)
    {
        // Start small; upload() can grow it.
        m_rtInstanceDataBuffers[i].create(m_ctx.device,
                                          m_ctx.physicalDevice,
                                          sizeof(RtInstanceData),
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                          false,
                                          false);

        // Bind once now; we will rewrite size later after upload.
        m_rtSets[i].writeStorageBuffer(m_ctx.device,
                                       4,
                                       m_rtInstanceDataBuffers[i].buffer(),
                                       m_rtInstanceDataBuffers[i].size(),
                                       0);
    }

    // Shared sampler for presenting the RT output
    if (m_rtSampler == VK_NULL_HANDLE)
    {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = VK_FILTER_LINEAR;
        sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod       = 0.0f;

        if (vkCreateSampler(device, &sci, nullptr, &m_rtSampler) != VK_SUCCESS)
        {
            std::cerr << "RendererVK: Failed to create RT present sampler.\n";
            return false;
        }
    }

    // if (!m_rtPipeline.createGradientPipeline(m_ctx, m_rtSetLayout.layout()))
    if (!m_rtPipeline.createScenePipeline(m_ctx, m_rtSetLayout.layout()))
    {
        std::cerr << "RendererVK: Failed to create RT gradient pipeline.\n";
        return false;
    }

    if (m_rtUploadPool == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_ctx.graphicsQueueFamilyIndex;

        if (vkCreateCommandPool(device, &pci, nullptr, &m_rtUploadPool) != VK_SUCCESS)
        {
            std::cerr << "RendererVK: Failed to create RT upload command pool.\n";
            return false;
        }
    }

    if (!m_rtSbt.buildAndUpload(m_ctx,
                                m_rtPipeline.pipeline(),
                                /*raygenCount*/ vkrt::RtPipeline::kRaygenCount,
                                /*missCount*/ vkrt::RtPipeline::kMissCount,
                                /*hitCount*/ vkrt::RtPipeline::kHitCount,
                                /*callable*/ vkrt::RtPipeline::kCallableCount,
                                m_rtUploadPool,
                                m_ctx.graphicsQueue))
    {
        std::cerr << "RendererVK: Failed to build/upload SBT.\n";
        return false;
    }

    // RT images are created lazily on first render (need width/height).

    return true;
}

//==================================================================
// RT present pipeline teardown
//==================================================================

void Renderer::destroyRtPresentPipeline() noexcept
{
    if (!m_ctx.device)
        return;

    VkDevice device = m_ctx.device;

    if (m_rtPresentPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_rtPresentPipeline, nullptr);
        m_rtPresentPipeline = VK_NULL_HANDLE;
    }

    if (m_rtPresentLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_rtPresentLayout, nullptr);
        m_rtPresentLayout = VK_NULL_HANDLE;
    }
}

//==================================================================
// RT scratch
//==================================================================

bool Renderer::ensureRtScratch(VkDeviceSize bytes) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device)
        return false;

    if (bytes == 0)
        return false;

    if (m_rtScratch.valid() && m_rtScratch.size() >= bytes)
        return true;

    m_rtScratch.destroy();
    m_rtScratchSize = 0;

    m_rtScratch.create(m_ctx.device,
                       m_ctx.physicalDevice,
                       bytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       /*persistentMap*/ false,
                       /*deviceAddress*/ true);

    if (!m_rtScratch.valid())
        return false;

    m_rtScratchSize = bytes;
    return true;
}

//==================================================================
// RT AS teardown (unchanged)
//==================================================================

void Renderer::destroyRtBlasFor(SceneMesh* sm) noexcept
{
    auto it = m_rtBlas.find(sm);
    if (it == m_rtBlas.end())
        return;

    RtBlas& b = it->second;

    if (b.as != VK_NULL_HANDLE)
    {
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);
        b.as = VK_NULL_HANDLE;
    }

    b.asBuffer.destroy(); // <-- CRITICAL

    b.address = 0;

    m_rtBlas.erase(it);
}

void Renderer::destroyAllRtBlas() noexcept
{
    if (!m_ctx.device || !m_ctx.rtDispatch)
        return;

    for (auto& [sm, b] : m_rtBlas)
    {
        if (b.as != VK_NULL_HANDLE)
            m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);

        b.as = VK_NULL_HANDLE;
        b.asBuffer.destroy();
        b.address  = 0;
        b.buildKey = 0;
    }

    m_rtBlas.clear();
}

void Renderer::destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept
{
    if (frameIndex >= m_rtTlasFrames.size())
        return;

    RtTlasFrame& t = m_rtTlasFrames[frameIndex];

    if (rtReady(m_ctx) && m_ctx.rtDispatch && m_ctx.device && t.as)
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, t.as, nullptr);

    t.as       = VK_NULL_HANDLE;
    t.address  = 0;
    t.buildKey = 0;

    t.buffer.destroy();

    if (destroyInstanceBuffers)
    {
        t.instanceBuffer.destroy();
        t.instanceStaging.destroy();
    }
}

void Renderer::destroyAllRtTlasFrames() noexcept
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_rtTlasFrames.size()); ++i)
        destroyRtTlasFrame(i, /*destroyInstanceBuffers*/ true);

    m_rtTlasFrames.clear();
}

//==================================================================
// Render
//==================================================================

void Renderer::render(VkCommandBuffer cmd, Viewport* vp, Scene* scene, uint32_t frameIndex)
{
    if (!vp || !scene)
        return;

    if (frameIndex >= m_framesInFlight)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());

    const glm::vec4 solidEdgeColor{0.10f, 0.10f, 0.10f, 0.5f};
    const glm::vec4 wireVisibleColor{0.85f, 0.85f, 0.85f, 1.0f};
    const glm::vec4 wireHiddenColor{0.85f, 0.85f, 0.85f, 0.25f};

    // ------------------------------------------------------------
    // RAY TRACE PRESENT PATH (EARLY OUT)
    // ------------------------------------------------------------
    if (vp->drawMode() == DrawMode::RAY_TRACE)
    {
        if (!rtReady(m_ctx))
            return;

        if (!ensureRtOutputImages(w, h))
            return;

        if (frameIndex >= m_rtSets.size())
            return;

        if (!m_rtPresentPipeline || !m_rtPresentLayout || m_rtSets.empty())
            return;

        vkutil::setViewportAndScissor(cmd, w, h);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rtPresentPipeline);

        VkDescriptorSet rtSet0 = m_rtSets[frameIndex].set();
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_rtPresentLayout,
                                0,
                                1,
                                &rtSet0,
                                0,
                                nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        // IMPORTANT: restore normal graphics set=0 binding/layout.
        {
            ViewportUboState& vpUbo = ensureViewportUboState(vp);

            if (frameIndex < vpUbo.mvpBuffers.size() &&
                frameIndex < vpUbo.uboSets.size() &&
                vpUbo.mvpBuffers[frameIndex].valid())
            {
                MvpUBO ubo{};
                ubo.proj = vp->projection();
                ubo.view = vp->view();
                vpUbo.mvpBuffers[frameIndex].upload(&ubo, sizeof(ubo));

                VkDescriptorSet gfxSet0 = vpUbo.uboSets[frameIndex].set();
                vkCmdBindDescriptorSets(cmd,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_pipelineLayout,
                                        0,
                                        1,
                                        &gfxSet0,
                                        0,
                                        nullptr);
            }
        }

        return;
    }

    // ------------------------------------------------------------
    // NORMAL GRAPHICS PATH (bind MVP set=0)
    // ------------------------------------------------------------

    ViewportUboState& vpUbo = ensureViewportUboState(vp);

    if (frameIndex >= vpUbo.mvpBuffers.size() || frameIndex >= vpUbo.uboSets.size())
        return;

    if (!vpUbo.mvpBuffers[frameIndex].valid())
        return;

    MvpUBO ubo{};
    ubo.proj = vp->projection();
    ubo.view = vp->view();
    vpUbo.mvpBuffers[frameIndex].upload(&ubo, sizeof(ubo));

    vkutil::setViewportAndScissor(cmd, w, h);

    {
        VkDescriptorSet set0 = vpUbo.uboSets[frameIndex].set();
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout,
                                0,
                                1,
                                &set0,
                                0,
                                nullptr);
    }

    // ------------------------------------------------------------
    // Solid / Shaded
    // ------------------------------------------------------------
    if (vp->drawMode() != DrawMode::WIREFRAME)
    {
        const bool isShaded = (vp->drawMode() == DrawMode::SHADED);

        VkPipeline triPipe = isShaded ? m_pipelineShaded : m_pipelineSolid;

        if (scene->materialHandler() && m_curMaterialCounter != scene->materialHandler()->changeCounter()->value())
        {
            const auto& mats = scene->materialHandler()->materials();
            for (uint32_t i = 0; i < m_ctx.framesInFlight; ++i)
            {
                uploadMaterialsToGpu(mats, *scene->textureHandler(), i);
                updateMaterialTextureTable(*scene->textureHandler(), i);
            }
            m_curMaterialCounter = scene->materialHandler()->changeCounter()->value();
        }

        {
            VkDescriptorSet set1 = m_materialSets[frameIndex].set();
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout,
                                    1,
                                    1,
                                    &set1,
                                    0,
                                    nullptr);
        }

        if (triPipe)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipe);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 1);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->vertexCount() == 0)
                        continue;

                    if (!gpu->polyVertBuffer().valid() ||
                        !gpu->polyNormBuffer().valid() ||
                        !gpu->polyUvPosBuffer().valid() ||
                        !gpu->polyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->polyVertBuffer().buffer(),
                        gpu->polyNormBuffer().buffer(),
                        gpu->polyUvPosBuffer().buffer(),
                        gpu->polyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->vertexCount(), 1, 0, 0);
                }
                else
                {
                    if (gpu->subdivPolyVertexCount() == 0)
                        continue;

                    if (!gpu->subdivPolyVertBuffer().valid() ||
                        !gpu->subdivPolyNormBuffer().valid() ||
                        !gpu->subdivPolyUvBuffer().valid() ||
                        !gpu->subdivPolyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->subdivPolyVertBuffer().buffer(),
                        gpu->subdivPolyNormBuffer().buffer(),
                        gpu->subdivPolyUvBuffer().buffer(),
                        gpu->subdivPolyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->subdivPolyVertexCount(), 1, 0, 0);
                }
            }
        }

        constexpr bool drawEdgesInSolid = true;
        if (!isShaded && drawEdgesInSolid && m_pipelineEdgeDepthBias)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineEdgeDepthBias);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = solidEdgeColor;

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->edgeIndexCount() == 0)
                        continue;

                    if (!gpu->uniqueVertBuffer().valid() || !gpu->edgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->uniqueVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->edgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->edgeIndexCount(), 1, 0, 0, 0);
                }
                else
                {
                    if (gpu->subdivPrimaryEdgeIndexCount() == 0)
                        continue;

                    if (!gpu->subdivSharedVertBuffer().valid() || !gpu->subdivPrimaryEdgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->subdivSharedVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->subdivPrimaryEdgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->subdivPrimaryEdgeIndexCount(), 1, 0, 0, 0);
                }
            }
        }
    }
    // ------------------------------------------------------------
    // Wireframe mode (hidden-line)
    // ------------------------------------------------------------
    else
    {
        // 1) depth-only triangles
        if (m_pipelineDepthOnly)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineDepthOnly);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu(); // static_cast<MeshGpuResourcesVK*>(sm->gpu());
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 0);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT |
                                       VK_SHADER_STAGE_GEOMETRY_BIT |
                                       VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->vertexCount() == 0)
                        continue;

                    if (!gpu->polyVertBuffer().valid() ||
                        !gpu->polyNormBuffer().valid() ||
                        !gpu->polyUvPosBuffer().valid() ||
                        !gpu->polyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->polyVertBuffer().buffer(),
                        gpu->polyNormBuffer().buffer(),
                        gpu->polyUvPosBuffer().buffer(),
                        gpu->polyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->vertexCount(), 1, 0, 0);
                }
                else
                {
                    // Subdiv depth pass uses the SAME buffers as solid subdiv
                    if (gpu->subdivPolyVertexCount() == 0)
                        continue;

                    if (!gpu->subdivPolyVertBuffer().valid() ||
                        !gpu->subdivPolyNormBuffer().valid() ||
                        !gpu->subdivPolyUvBuffer().valid() ||
                        !gpu->subdivPolyMatIdBuffer().valid())
                    {
                        continue;
                    }

                    VkBuffer bufs[4] = {
                        gpu->subdivPolyVertBuffer().buffer(),
                        gpu->subdivPolyNormBuffer().buffer(),
                        gpu->subdivPolyUvBuffer().buffer(),
                        gpu->subdivPolyMatIdBuffer().buffer(),
                    };
                    VkDeviceSize offs[4] = {0, 0, 0, 0};

                    vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                    vkCmdDraw(cmd, gpu->subdivPolyVertexCount(), 1, 0, 0);
                }
            }
        }

        auto drawEdges = [&](VkPipeline pipeline, const glm::vec4& color) {
            if (!pipeline)
                return;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            for (SceneMesh* sm : scene->sceneMeshes())
            {
                if (!sm->visible())
                    continue;

                if (!sm->gpu())
                    sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

                auto* gpu = sm->gpu();
                gpu->update();

                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = color;

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                if (!useSubdiv)
                {
                    if (gpu->edgeIndexCount() == 0)
                        continue;

                    if (!gpu->uniqueVertBuffer().valid() || !gpu->edgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->uniqueVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->edgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->edgeIndexCount(), 1, 0, 0, 0);
                }
                else
                {
                    if (gpu->subdivPrimaryEdgeIndexCount() == 0)
                        continue;

                    if (!gpu->subdivSharedVertBuffer().valid() || !gpu->subdivPrimaryEdgeIndexBuffer().valid())
                        continue;

                    VkBuffer     vbuf = gpu->subdivSharedVertBuffer().buffer();
                    VkDeviceSize voff = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);

                    vkCmdBindIndexBuffer(cmd, gpu->subdivPrimaryEdgeIndexBuffer().buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, gpu->subdivPrimaryEdgeIndexCount(), 1, 0, 0, 0);
                }
            }
        };

        // 2) hidden edges (GREATER) dim
        drawEdges(m_pipelineEdgeHidden, wireHiddenColor);

        // 3) visible edges (LEQUAL) normal
        drawEdges(m_pipelineWire, wireVisibleColor);
    }

    // Selection overlay
    drawSelection(cmd, vp, scene);

    // Scene Grid (draw last) - NOT in SHADED mode
    if (scene->showSceneGrid() && vp->drawMode() != DrawMode::SHADED)
    {
        drawSceneGrid(cmd, vp, scene);
    }
}

//==================================================================
// RT dispatch (writes per-frame RT image, then present samples it)
//==================================================================

void Renderer::renderRayTrace(Viewport* vp, VkCommandBuffer cmd, Scene* scene, uint32_t frameIndex)
{
    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return;

    if (!cmd || !vp || !scene)
        return;

    if (!m_rtPipeline.valid() || !m_rtSbt.buffer())
        return;

    if (frameIndex >= m_rtCameraBuffers.size())
        return;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());
    if (w == 0 || h == 0)
        return;

    if (!ensureRtOutputImages(w, h))
        return;

    if (frameIndex >= m_rtImages.size())
        return;

    RtImagePerFrame& out = m_rtImages[frameIndex];
    if (!out.image || !out.view)
        return;

    // ------------------------------------------------------------
    // Clear RT output to viewport background (even if we early-out).
    // Must be outside a render pass -> renderPrePass() is correct.
    // ------------------------------------------------------------
    {
        const VkClearColorValue clear = vkutil::toVkClearColor(vp->clearColor());

        VkImageSubresourceRange range = {};
        range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel            = 0;
        range.levelCount              = 1;
        range.baseArrayLayer          = 0;
        range.layerCount              = 1;

        if (out.needsInit)
        {
            imageBarrier(cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_GENERAL,
                         0,
                         VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);

            out.needsInit = false;
        }
        else
        {
            imageBarrier(cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        vkCmdClearColorImage(cmd, out.image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);

        imageBarrier(cmd,
                     out.image,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    // ------------------------------------------------------------
    // Ensure mesh GPU resources are up-to-date and build BLAS
    // ------------------------------------------------------------
    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        if (!sm->gpu())
            sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

        MeshGpuResources* gpu = sm->gpu();
        if (!gpu)
            continue;

        gpu->update();

        const RtMeshGeometry geo = selectRtGeometry(sm);
        if (!geo.valid())
            continue;

        if (!ensureMeshBlas(sm, geo, cmd))
            continue;
    }

    // TLAS must exist; if it fails, we keep output readable.
    if (!ensureSceneTlas(scene, cmd, frameIndex))
        return;

    if (frameIndex >= m_rtTlasFrames.size() || m_rtTlasFrames[frameIndex].as == VK_NULL_HANDLE)
        return;

    // Make set=0 binding=3 valid for this frame
    writeRtTlasDescriptor(frameIndex);

    // ------------------------------------------------------------
    // Upload per-instance shader data (coarse OR subdiv addresses)
    // ------------------------------------------------------------
    {
        std::vector<RtInstanceData> instData;
        instData.reserve(scene->sceneMeshes().size());

        for (SceneMesh* sm : scene->sceneMeshes())
        {
            if (!sm || !sm->visible())
                continue;

            auto it = m_rtBlas.find(sm);
            if (it == m_rtBlas.end())
                continue;

            const RtBlas& b = it->second;
            if (b.as == VK_NULL_HANDLE || b.address == 0)
                continue;

            const RtMeshGeometry geo = selectRtGeometry(sm);
            if (!geo.valid() || !geo.shaderValid())
                continue;

            const uint32_t primCount = geo.buildIndexCount / 3u;
            if (primCount == 0)
                continue;

            // TLAS uses primitiveCount derived from buildIndexCount; shading must match.
            if (geo.shaderTriCount != primCount)
                continue;

            // Corner normals/uvs must match primitive order: triCount*3
            if (geo.shadeNrmCount != primCount * 3u)
                continue;

            // if (geo.shadeUvCount != primCount * 3u)
            // continue;

            RtInstanceData d = {};
            d.posAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadePosBuffer);
            d.idxAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shaderIndexBuffer);
            d.nrmAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeNrmBuffer);
            d.uvAdr          = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeUvBuffer);
            d.triCount       = geo.shaderTriCount;

            assert(geo.shadeUvBuffer != VK_NULL_HANDLE); // if you expect it
            assert(d.uvAdr != 0);                        // after you fix flags/allocation

            // if (d.posAdr == 0 || d.idxAdr == 0 || d.nrmAdr == 0 || d.uvAdr == 0 || d.triCount == 0)
            // continue;
            // d.uvAdr = 0; // keep it simple until UV step
            if (d.posAdr == 0 || d.idxAdr == 0 || d.nrmAdr == 0 || d.triCount == 0)
                continue;

            instData.push_back(d);
        }

        if (!instData.empty())
        {
            const VkDeviceSize bytes = VkDeviceSize(instData.size() * sizeof(RtInstanceData));
            m_rtInstanceDataBuffers[frameIndex].upload(instData.data(), bytes);

            m_rtSets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                    4,
                                                    m_rtInstanceDataBuffers[frameIndex].buffer(),
                                                    bytes,
                                                    0);
        }
    }

    // ------------------------------------------------------------
    // Update RT camera UBO
    // ------------------------------------------------------------
    RtCameraUBO cam = {};
    cam.invViewProj = glm::inverse(vp->projection() * vp->view());
    cam.camPos      = glm::vec4(vp->cameraPosition(), 1.0f);

    m_rtCameraBuffers[frameIndex].upload(&cam, sizeof(cam));

    // ------------------------------------------------------------
    // Transition for raygen writes
    // ------------------------------------------------------------
    imageBarrier(cmd,
                 out.image,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_SHADER_READ_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline.pipeline());

    VkDescriptorSet set0 = m_rtSets[frameIndex].set();
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_rtPipeline.layout(),
                            0,
                            1,
                            &set0,
                            0,
                            nullptr);

    VkStridedDeviceAddressRegionKHR rgen = {};
    VkStridedDeviceAddressRegionKHR miss = {};
    VkStridedDeviceAddressRegionKHR hit  = {};
    VkStridedDeviceAddressRegionKHR call = {};
    m_rtSbt.regions(m_ctx, rgen, miss, hit, call);

    m_ctx.rtDispatch->vkCmdTraceRaysKHR(cmd,
                                        &rgen,
                                        &miss,
                                        &hit,
                                        &call,
                                        w,
                                        h,
                                        1);

    // ------------------------------------------------------------
    // Transition back for present sampling
    // ------------------------------------------------------------
    imageBarrier(cmd,
                 out.image,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

//==================================================================
// Render before begin frame (before the render pass)
//==================================================================

void Renderer::renderPrePass(Viewport* vp, VkCommandBuffer cmd, Scene* scene, uint32_t frameIndex)
{
    if (!vp || !cmd || !scene)
        return;

    if (vp->drawMode() != DrawMode::RAY_TRACE)
        return;

    if (!rtReady(m_ctx))
        return;

    renderRayTrace(vp, cmd, scene, frameIndex);
}

bool Renderer::ensureMeshBlas(SceneMesh* sm, const RtMeshGeometry& geo, VkCommandBuffer cmd) noexcept
{
    if (!sm || !cmd)
        return false;

    if (!rtReady(m_ctx) || !m_ctx.rtDispatch || !m_ctx.device)
        return false;

    if (!geo.valid())
        return false;

    SysMesh* mesh = sm->sysMesh();
    if (!mesh)
        return false;

    const int32_t  subdivLevel = sm->subdivisionLevel();
    const uint64_t topo        = mesh->topology_counter()->value();
    const uint64_t deform      = mesh->deform_counter()->value();

    // ------------------------------------------------------------
    // Build key (buffer identity + counts + mesh counters + subdiv)
    // ------------------------------------------------------------
    uint64_t key    = 1469598103934665603ull; // FNV-1a basis
    auto     fnvMix = [&](uint64_t v) noexcept {
        key ^= v;
        key *= 1099511628211ull;
    };

    fnvMix(reinterpret_cast<uint64_t>(geo.buildPosBuffer));
    fnvMix(static_cast<uint64_t>(geo.buildPosCount));
    fnvMix(reinterpret_cast<uint64_t>(geo.buildIndexBuffer));
    fnvMix(static_cast<uint64_t>(geo.buildIndexCount));
    fnvMix(static_cast<uint64_t>(static_cast<uint32_t>(subdivLevel)));
    fnvMix(topo);
    fnvMix(deform);

    if (!kRtRebuildAsEveryFrame)
    {
        auto it = m_rtBlas.find(sm);
        if (it != m_rtBlas.end())
        {
            RtBlas& b = it->second;
            if (b.as != VK_NULL_HANDLE && b.address != 0 && b.buildKey == key)
                return true;
        }
    }

    // Always rebuild from scratch when key changed (or debug)
    destroyRtBlasFor(sm);

    VkAccelerationStructureGeometryKHR asGeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureGeometryTrianglesDataKHR& tri = asGeom.geometry.triangles;
    tri.sType                                            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat                                     = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexStride                                     = sizeof(glm::vec3);
    tri.maxVertex                                        = (geo.buildPosCount > 0) ? (geo.buildPosCount - 1) : 0;
    tri.indexType                                        = VK_INDEX_TYPE_UINT32;

    const VkDeviceAddress vAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildPosBuffer);
    const VkDeviceAddress iAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildIndexBuffer);
    if (vAdr == 0 || iAdr == 0)
        return false;

    tri.vertexData.deviceAddress = vAdr;
    tri.indexData.deviceAddress  = iAdr;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &asGeom;

    const uint32_t primCount = geo.buildIndexCount / 3u;

    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_ctx.rtDispatch->vkGetAccelerationStructureBuildSizesKHR(m_ctx.device,
                                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                              &buildInfo,
                                                              &primCount,
                                                              &sizes);

    if (sizes.accelerationStructureSize == 0 || sizes.buildScratchSize == 0)
        return false;

    RtBlas blas = {};

    blas.asBuffer.create(m_ctx.device,
                         m_ctx.physicalDevice,
                         sizes.accelerationStructureSize,
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         /*persistentMap*/ false,
                         /*deviceAddress*/ true);

    if (!blas.asBuffer.valid())
        return false;

    VkAccelerationStructureCreateInfoKHR asci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asci.size   = sizes.accelerationStructureSize;
    asci.buffer = blas.asBuffer.buffer();

    if (m_ctx.rtDispatch->vkCreateAccelerationStructureKHR(m_ctx.device, &asci, nullptr, &blas.as) != VK_SUCCESS)
        return false;

    if (!ensureRtScratch(sizes.buildScratchSize))
    {
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, blas.as, nullptr);
        blas.as = VK_NULL_HANDLE;
        blas.asBuffer.destroy();
        return false;
    }

    const VkDeviceAddress scratchAdr = vkutil::bufferDeviceAddress(m_ctx.device, m_rtScratch);
    if (scratchAdr == 0)
    {
        m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, blas.as, nullptr);
        blas.as = VK_NULL_HANDLE;
        blas.asBuffer.destroy();
        return false;
    }

    buildInfo.dstAccelerationStructure  = blas.as;
    buildInfo.scratchData.deviceAddress = scratchAdr;

    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount                           = primCount;

    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {&range};
    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);

    // Make BLAS readable by TLAS build + RT shaders
    {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addrInfo.accelerationStructure = blas.as;
    blas.address                   = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);

    blas.buildKey = key;
    m_rtBlas[sm]  = std::move(blas);

    return (m_rtBlas[sm].as != VK_NULL_HANDLE && m_rtBlas[sm].address != 0);
}

bool Renderer::ensureSceneTlas(Scene* scene, VkCommandBuffer cmd, uint32_t frameIndex) noexcept
{
    if (!scene || !cmd)
        return false;

    if (!rtReady(m_ctx) || !m_ctx.rtDispatch || !m_ctx.device)
        return false;

    if (frameIndex >= m_rtSets.size())
        return false;

    if (frameIndex >= m_rtTlasFrames.size())
        return false;

    RtTlasFrame& tlas = m_rtTlasFrames[frameIndex];

    // ------------------------------------------------------------
    // 1) Gather instances + build key
    // ------------------------------------------------------------
    struct InstanceSrc
    {
        SceneMesh*      sm      = nullptr;
        VkDeviceAddress blasAdr = 0;
        glm::mat4       model   = glm::mat4(1.0f);
    };

    std::vector<InstanceSrc> src;
    src.reserve(scene->sceneMeshes().size());

    uint64_t sceneKey = 1469598103934665603ull; // FNV-1a
    auto     fnvMix   = [&](uint64_t v) noexcept {
        sceneKey ^= v;
        sceneKey *= 1099511628211ull;
    };

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        auto it = m_rtBlas.find(sm);
        if (it == m_rtBlas.end())
            continue;

        const RtBlas& b = it->second;
        if (b.as == VK_NULL_HANDLE || b.address == 0)
            continue;

        // const RtMeshGeometry geo = selectRtGeometry(sm);
        // if (!geo.valid())
        //     continue;

        // const uint32_t primCount = geo.buildIndexCount / 3u;
        // if (primCount == 0)
        //     continue;

        // if (geo.shaderTriCount != primCount)
        //     continue;
        const RtMeshGeometry geo = selectRtGeometry(sm);
        if (!geo.valid() || !geo.shaderValid())
            continue;

        const uint32_t primCount = geo.buildIndexCount / 3u;
        if (primCount == 0)
            continue;

        if (geo.shaderTriCount != primCount)
            continue;

        if (geo.shadeNrmCount != primCount * 3u)
            continue;

        // IMPORTANT: do NOT require UV here yet

        InstanceSrc s = {};
        s.sm          = sm;
        s.blasAdr     = b.address;
        s.model       = sm->model();
        src.push_back(s);

        // Key includes BLAS address + transform
        fnvMix(static_cast<uint64_t>(s.blasAdr));
        const uint32_t* m = reinterpret_cast<const uint32_t*>(&s.model);
        for (int i = 0; i < 16; ++i)
            fnvMix(static_cast<uint64_t>(m[i]));
    }

    // Mix in global TLAS dirty counter (topo/deform parented into this)
    fnvMix(static_cast<uint64_t>(m_rtTlasChangeCounter->value()));

    if (src.empty())
        return true;

    // ------------------------------------------------------------
    // Fast path
    // ------------------------------------------------------------
    if (!kRtRebuildAsEveryFrame)
    {
        if (tlas.as != VK_NULL_HANDLE && tlas.buildKey == sceneKey)
        {
            writeTlasDescriptor(m_ctx.device, m_rtSets[frameIndex].set(), tlas.as);
            return true;
        }
    }

    // ------------------------------------------------------------
    // 2) Build VkAccelerationStructureInstanceKHR array
    // ------------------------------------------------------------
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.resize(src.size());

    for (size_t i = 0; i < src.size(); ++i)
    {
        VkAccelerationStructureInstanceKHR inst     = {};
        inst.transform                              = vkutil::toVkTransformMatrix(src[i].model);
        inst.instanceCustomIndex                    = static_cast<uint32_t>(i);
        inst.mask                                   = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference         = src[i].blasAdr;
        instances[i]                                = inst;
    }

    const VkDeviceSize instanceBytes =
        static_cast<VkDeviceSize>(instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

    // ------------------------------------------------------------
    // 3) (Re)create per-frame instance buffers
    // ------------------------------------------------------------
    if (!tlas.instanceStaging.valid() || tlas.instanceStaging.size() < instanceBytes)
    {
        tlas.instanceStaging.destroy();
        tlas.instanceStaging.create(m_ctx.device,
                                    m_ctx.physicalDevice,
                                    instanceBytes,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    /*persistentMap*/ true,
                                    /*deviceAddress*/ false);
        if (!tlas.instanceStaging.valid())
            return false;
    }

    if (!tlas.instanceBuffer.valid() || tlas.instanceBuffer.size() < instanceBytes)
    {
        tlas.instanceBuffer.destroy();
        tlas.instanceBuffer.create(m_ctx.device,
                                   m_ctx.physicalDevice,
                                   instanceBytes,
                                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   /*persistentMap*/ false,
                                   /*deviceAddress*/ true);
        if (!tlas.instanceBuffer.valid())
            return false;
    }

    tlas.instanceStaging.upload(instances.data(), instanceBytes);

    VkBufferCopy copy = {};
    copy.srcOffset    = 0;
    copy.dstOffset    = 0;
    copy.size         = instanceBytes;
    vkCmdCopyBuffer(cmd, tlas.instanceStaging.buffer(), tlas.instanceBuffer.buffer(), 1, &copy);

    {
        VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bb.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        bb.dstAccessMask       = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.buffer              = tlas.instanceBuffer.buffer();
        bb.offset              = 0;
        bb.size                = instanceBytes;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0,
                             0,
                             nullptr,
                             1,
                             &bb,
                             0,
                             nullptr);
    }

    const VkDeviceAddress instanceAdr = vkutil::bufferDeviceAddress(m_ctx.device, tlas.instanceBuffer);
    if (instanceAdr == 0)
        return false;

    // ------------------------------------------------------------
    // 4) Rebuild per-frame TLAS
    // ------------------------------------------------------------
    destroyRtTlasFrame(frameIndex, /*destroyInstanceBuffers*/ false);

    VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geom.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.arrayOfPointers    = VK_FALSE;
    geom.geometry.instances.data.deviceAddress = instanceAdr;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geom;

    const uint32_t primCount = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_ctx.rtDispatch->vkGetAccelerationStructureBuildSizesKHR(m_ctx.device,
                                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                              &buildInfo,
                                                              &primCount,
                                                              &sizes);

    if (sizes.accelerationStructureSize == 0 || sizes.buildScratchSize == 0)
        return false;

    tlas.buffer.create(m_ctx.device,
                       m_ctx.physicalDevice,
                       sizes.accelerationStructureSize,
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       /*persistentMap*/ false,
                       /*deviceAddress*/ true);

    if (!tlas.buffer.valid())
        return false;

    VkAccelerationStructureCreateInfoKHR asci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    asci.buffer = tlas.buffer.buffer();
    asci.size   = sizes.accelerationStructureSize;
    asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (m_ctx.rtDispatch->vkCreateAccelerationStructureKHR(m_ctx.device, &asci, nullptr, &tlas.as) != VK_SUCCESS)
        return false;

    if (!ensureRtScratch(sizes.buildScratchSize))
        return false;

    const VkDeviceAddress scratchAdr = vkutil::bufferDeviceAddress(m_ctx.device, m_rtScratch);
    if (scratchAdr == 0)
        return false;

    buildInfo.dstAccelerationStructure  = tlas.as;
    buildInfo.scratchData.deviceAddress = scratchAdr;

    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount                           = primCount;

    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {&range};
    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);

    {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    {
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        addrInfo.accelerationStructure = tlas.as;
        tlas.address                   = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    }

    writeTlasDescriptor(m_ctx.device, m_rtSets[frameIndex].set(), tlas.as);

    tlas.buildKey = sceneKey;
    return true;
}

void Renderer::writeRtTlasDescriptor(uint32_t frameIndex) noexcept
{
    if (!m_ctx.device)
        return;

    if (frameIndex >= m_rtSets.size())
        return;

    if (frameIndex >= m_rtTlasFrames.size())
        return;

    RtTlasFrame& tf = m_rtTlasFrames[frameIndex];

    // Need a valid TLAS handle to write.
    if (!tf.as)
        return;

    VkDescriptorSet set0 = m_rtSets[frameIndex].set();
    if (!set0)
        return;

    VkWriteDescriptorSetAccelerationStructureKHR asInfo = {};
    asInfo.sType                                        = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount                   = 1;
    asInfo.pAccelerationStructures                      = &tf.as;

    VkWriteDescriptorSet w = {};
    w.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.pNext                = &asInfo;
    w.dstSet               = set0;
    w.dstBinding           = 3; // uTlas
    w.dstArrayElement      = 0;
    w.descriptorCount      = 1;
    w.descriptorType       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    vkUpdateDescriptorSets(m_ctx.device, 1, &w, 0, nullptr);
}

static uint32_t findMemTypeForImage(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }

    return UINT32_MAX;
}

//==================================================================
// Selection overlay
//==================================================================

void Renderer::drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene)
{
    if (!scene || !vp)
        return;

    if (!m_pipelineSelVert && !m_pipelineSelEdge && !m_pipelineSelPoly)
        return;

    VkDeviceSize zeroOffset = 0;

    const glm::vec4 selColorVisible = glm::vec4(1.0f, 0.55f, 0.10f, 0.6f);
    const glm::vec4 selColorHidden  = glm::vec4(1.0f, 0.55f, 0.10f, 0.3f);

    const bool showOccluded = (vp->drawMode() == DrawMode::WIREFRAME);

    auto pushPC = [&](SceneMesh* sm, const glm::vec4& color) {
        PushConstants pc = {};
        pc.model         = sm->model();
        pc.color         = color;

        vkCmdPushConstants(cmd,
                           m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(PushConstants),
                           &pc);
    };

    auto drawHidden = [&](SceneMesh* sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!showOccluded)
            return;
        if (!pipeline || indexCount == 0)
            return;

        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        pushPC(sm, selColorHidden);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    auto drawVisible = [&](SceneMesh* sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!pipeline || indexCount == 0)
            return;

        vkCmdSetDepthBias(cmd, -1.0f, 0.0f, -1.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        pushPC(sm, selColorVisible);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->visible())
            continue;

        if (!sm->gpu())
            sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

        MeshGpuResources* gpu = sm->gpu();
        gpu->update();

        const bool useSubdiv = (sm->subdivisionLevel() > 0);

        // ---------------------------------------------------------
        // Choose position buffer + selection index buffers
        // ---------------------------------------------------------
        VkBuffer   posVb    = VK_NULL_HANDLE;
        uint32_t   selCount = 0;
        VkBuffer   selIb    = VK_NULL_HANDLE;
        VkPipeline pipeVis  = VK_NULL_HANDLE;
        VkPipeline pipeHid  = VK_NULL_HANDLE;

        const SelectionMode mode = scene->selectionMode();

        if (!useSubdiv)
        {
            if (gpu->uniqueVertCount() == 0 || !gpu->uniqueVertBuffer().valid())
                continue;

            posVb = gpu->uniqueVertBuffer().buffer();

            if (mode == SelectionMode::VERTS)
            {
                if (gpu->selVertIndexCount() == 0 || !gpu->selVertIndexBuffer().valid())
                    continue;

                selCount = gpu->selVertIndexCount();
                selIb    = gpu->selVertIndexBuffer().buffer();
                pipeVis  = m_pipelineSelVert;
                pipeHid  = m_pipelineSelVertHidden;
            }
            else if (mode == SelectionMode::EDGES)
            {
                if (gpu->selEdgeIndexCount() == 0 || !gpu->selEdgeIndexBuffer().valid())
                    continue;

                selCount = gpu->selEdgeIndexCount();
                selIb    = gpu->selEdgeIndexBuffer().buffer();
                pipeVis  = m_pipelineSelEdge;
                pipeHid  = m_pipelineSelEdgeHidden;
            }
            else if (mode == SelectionMode::POLYS)
            {
                if (gpu->selPolyIndexCount() == 0 || !gpu->selPolyIndexBuffer().valid())
                    continue;

                selCount = gpu->selPolyIndexCount();
                selIb    = gpu->selPolyIndexBuffer().buffer();
                pipeVis  = m_pipelineSelPoly;
                pipeHid  = m_pipelineSelPolyHidden;
            }
            else
            {
                continue;
            }
        }
        else
        {
            // Subdiv selection indices are into the subdiv shared vertex buffer.
            if (gpu->subdivSharedVertCount() == 0 || !gpu->subdivSharedVertBuffer().valid())
                continue;

            posVb = gpu->subdivSharedVertBuffer().buffer();

            if (mode == SelectionMode::VERTS)
            {
                if (gpu->subdivSelVertIndexCount() == 0 || !gpu->subdivSelVertIndexBuffer().valid())
                    continue;

                selCount = gpu->subdivSelVertIndexCount();
                selIb    = gpu->subdivSelVertIndexBuffer().buffer();
                pipeVis  = m_pipelineSelVert;
                pipeHid  = m_pipelineSelVertHidden;
            }
            else if (mode == SelectionMode::EDGES)
            {
                if (gpu->subdivSelEdgeIndexCount() == 0 || !gpu->subdivSelEdgeIndexBuffer().valid())
                    continue;

                selCount = gpu->subdivSelEdgeIndexCount();
                selIb    = gpu->subdivSelEdgeIndexBuffer().buffer();
                pipeVis  = m_pipelineSelEdge;
                pipeHid  = m_pipelineSelEdgeHidden;
            }
            else if (mode == SelectionMode::POLYS)
            {
                if (gpu->subdivSelPolyIndexCount() == 0 || !gpu->subdivSelPolyIndexBuffer().valid())
                    continue;

                selCount = gpu->subdivSelPolyIndexCount();
                selIb    = gpu->subdivSelPolyIndexBuffer().buffer();
                pipeVis  = m_pipelineSelPoly;
                pipeHid  = m_pipelineSelPolyHidden;
            }
            else
            {
                continue;
            }
        }

        // ---------------------------------------------------------
        // Bind + draw
        // ---------------------------------------------------------
        vkCmdBindVertexBuffers(cmd, 0, 1, &posVb, &zeroOffset);
        vkCmdBindIndexBuffer(cmd, selIb, 0, VK_INDEX_TYPE_UINT32);

        drawHidden(sm, pipeHid, selCount);
        drawVisible(sm, pipeVis, selCount);
    }

    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
}

//==================================================================
// Overlay lines
//==================================================================

void Renderer::ensureOverlayVertexCapacity(std::size_t requiredVertexCount)
{
    if (requiredVertexCount == 0)
        return;

    if (requiredVertexCount <= m_overlayVertexCapacity && m_overlayVertexBuffer.valid())
        return;

    if (m_overlayVertexBuffer.valid())
        m_overlayVertexBuffer.destroy();

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(requiredVertexCount * sizeof(OverlayVertex));

    m_overlayVertexBuffer.create(m_ctx.device,
                                 m_ctx.physicalDevice,
                                 bufferSize,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 /*persistentMap*/ true);

    if (!m_overlayVertexBuffer.valid())
    {
        m_overlayVertexCapacity = 0;
        return;
    }

    m_overlayVertexCapacity = requiredVertexCount;
}

void Renderer::drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays)
{
    if (!vp)
        return;

    const auto& lines = overlays.lines();
    if (lines.empty())
        return;

    std::vector<OverlayVertex> vertices;
    vertices.reserve(lines.size() * 2);

    for (const auto& L : lines)
    {
        OverlayVertex v0{};
        v0.pos       = L.p1;
        v0.thickness = L.thickness;
        v0.color     = L.color;
        vertices.push_back(v0);

        OverlayVertex v1{};
        v1.pos       = L.p2;
        v1.thickness = L.thickness;
        v1.color     = L.color;
        vertices.push_back(v1);
    }

    const std::size_t vertexCount = vertices.size();
    if (vertexCount == 0)
        return;

    ensureOverlayVertexCapacity(vertexCount);
    if (!m_overlayVertexBuffer.valid())
        return;

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(vertexCount * sizeof(OverlayVertex));
    m_overlayVertexBuffer.upload(vertices.data(), byteSize);

    if (!m_overlayLinePipeline)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_overlayLinePipeline);

    PushConstants pc{};
    pc.model         = glm::mat4(1.0f);
    pc.color         = glm::vec4(1.0f);
    pc.overlayParams = glm::vec4(static_cast<float>(vp->width()),
                                 static_cast<float>(vp->height()),
                                 1.0f,
                                 0.0f);

    vkCmdPushConstants(cmd,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &pc);

    VkBuffer     vb      = m_overlayVertexBuffer.buffer();
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset0);

    vkCmdDraw(cmd, static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

void Renderer::drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    if (!scene->showSceneGrid())
        return;

    if (!m_grid)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    // ------------------------------------------------------------
    // Orient the grid depending on the viewport view mode.
    // Grid geometry is authored on XZ (Y=0) as a floor.
    // For FRONT/LEFT/etc we rotate it so it becomes XY or YZ.
    // ------------------------------------------------------------
    glm::mat4 gridModel = glm::mat4(1.0f);

    const float halfPi = glm::half_pi<float>();

    switch (vp->viewMode())
    {
        case ViewMode::TOP:
            // XZ plane (default)
            gridModel = glm::mat4(1.0f);
            break;

        case ViewMode::BOTTOM:
            // still XZ, but flip it
            gridModel = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            break;

        case ViewMode::FRONT:
            // want XY plane -> rotate XZ around +X by -90°
            gridModel = glm::rotate(glm::mat4(1.0f), -halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            break;

        case ViewMode::BACK:
            // XY plane, opposite
            gridModel = glm::rotate(glm::mat4(1.0f), +halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            break;

        case ViewMode::LEFT:
            // want YZ plane -> rotate XZ around +Z by +90°
            gridModel = glm::rotate(glm::mat4(1.0f), +halfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            break;

        case ViewMode::RIGHT:
            // YZ plane, opposite
            gridModel = glm::rotate(glm::mat4(1.0f), -halfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            break;

        default:
            // Perspective / other: treat as floor grid
            gridModel = glm::mat4(1.0f);
            break;
    }

    PushConstants pc{};
    pc.model = gridModel;
    pc.color = glm::vec4(0, 0, 0, 0);

    vkCmdPushConstants(cmd,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &pc);

    m_grid->render(cmd);
}
#endif
