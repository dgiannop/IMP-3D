#include "Renderer.hpp"

#include <Sysmesh.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

#include "GpuResources/GpuMaterial.hpp"
#include "GpuResources/MeshGpuResources.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "GridRendererVK.hpp"
#include "RenderGeometry.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "ShaderStage.hpp"
#include "Viewport.hpp"
#include "VkPipelineHelpers.hpp"
#include "VkUtilities.hpp"

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

    // Per-viewport scratch
    for (GpuBuffer& b : scratchBuffers)
        b.destroy();
    scratchBuffers.clear();
    scratchSizes.clear();

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
    if (m_ctx.device)
        vkDeviceWaitIdle(m_ctx.device);

    destroySwapchainResources();

    // Per-viewport MVP + Lights state
    for (auto& [vp, state] : m_viewportUbos)
    {
        for (auto& buf : state.mvpBuffers)
            buf.destroy();
        state.mvpBuffers.clear();

        for (auto& buf : state.lightBuffers)
            buf.destroy();
        state.lightBuffers.clear();

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

    m_ctx = {};

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
    if (!device)
        return false;

    m_framesInFlight  = std::max(1u, framesInFlight);
    const uint32_t fi = m_framesInFlight;

    // ------------------------------------------------------------
    // Destroy/recreate (safe on resize / re-init)
    // IMPORTANT: clear cached per-viewport sets because they are tied to the pool/layout.
    // ------------------------------------------------------------
    m_viewportUbos.clear();

    m_descriptorPool.destroy();
    m_descriptorSetLayout.destroy();
    m_materialSetLayout.destroy();

    // ------------------------------------------------------------
    // set=0 : per-viewport UBOs
    //   binding 0 = MVP
    //   binding 1 = Lights
    // ------------------------------------------------------------
    {
        DescriptorBindingInfo uboBindings[2]{};

        uboBindings[0].binding = 0;
        uboBindings[0].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // MVP is used by VS; keep GS if we have a geo stage consuming it.
        uboBindings[0].stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
        uboBindings[0].count  = 1;

        uboBindings[1].binding = 1;
        uboBindings[1].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // Lights: FS typically; keep GS if any overlay/geo path reads them.
        uboBindings[1].stages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
        uboBindings[1].count  = 1;

        if (!m_descriptorSetLayout.create(device, std::span{uboBindings, 2}))
        {
            std::cerr << "RendererVK: Failed to create UBO DescriptorSetLayout.\n";
            return false;
        }
    }

    // ------------------------------------------------------------
    // set=1 : materials
    //   binding 0 = materials SSBO
    //   binding 1 = texture table (sampler array)
    // ------------------------------------------------------------
    {
        DescriptorBindingInfo matBindings[2]{};

        matBindings[0].binding = 0;
        matBindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        matBindings[0].stages  = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        matBindings[0].count   = 1;

        matBindings[1].binding = 1;
        matBindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        matBindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        matBindings[1].count   = kMaxTextureCount;

        if (!m_materialSetLayout.create(device, std::span{matBindings, 2}))
        {
            std::cerr << "RendererVK: Failed to create material DescriptorSetLayout.\n";
            return false;
        }
    }

    // ------------------------------------------------------------
    // Pool sizes
    //
    // Raster UBO sets: (maxViewports * frames) each with 2 UBO bindings.
    // Material sets: (frames) each with 1 SSBO + kMaxTextureCount samplers.
    // ------------------------------------------------------------
    const uint32_t rasterSetCount   = fi * kMaxViewports;
    const uint32_t materialSetCount = fi;

    std::array<VkDescriptorPoolSize, 3> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rasterSetCount * 2u},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, materialSetCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialSetCount * kMaxTextureCount},
    };

    const uint32_t maxSets = rasterSetCount + materialSetCount;

    if (!m_descriptorPool.create(device, poolSizes, maxSets))
    {
        std::cerr << "RendererVK: Failed to create shared DescriptorPool.\n";
        return false;
    }

    // ------------------------------------------------------------
    // Allocate per-frame material sets (set=1)
    // ------------------------------------------------------------
    m_materialSets.clear();
    m_materialSets.resize(fi);

    for (uint32_t i = 0; i < fi; ++i)
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
    ViewportUboState& s = m_viewportUbos[vp];

    if (s.mvpBuffers.size() != m_framesInFlight)
        s.mvpBuffers.resize(m_framesInFlight);
    if (s.lightBuffers.size() != m_framesInFlight)
        s.lightBuffers.resize(m_framesInFlight);
    if (s.uboSets.size() != m_framesInFlight)
        s.uboSets.resize(m_framesInFlight);

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        bool needWrite = false;

        if (s.uboSets[i].set() == VK_NULL_HANDLE)
        {
            if (!s.uboSets[i].allocate(m_ctx.device, m_descriptorPool.pool(), m_descriptorSetLayout.layout()))
            {
                std::cerr << "RendererVK: Failed to allocate raster UBO set for viewport frame " << i << ".\n";
                continue;
            }
            needWrite = true;
        }

        if (!s.mvpBuffers[i].valid())
        {
            s.mvpBuffers[i].create(m_ctx.device,
                                   m_ctx.physicalDevice,
                                   sizeof(MvpUBO),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   true);
            if (!s.mvpBuffers[i].valid())
            {
                std::cerr << "RendererVK: Failed to create MVP UBO for viewport frame " << i << ".\n";
                continue;
            }
            needWrite = true;
        }

        if (!s.lightBuffers[i].valid())
        {
            s.lightBuffers[i].create(m_ctx.device,
                                     m_ctx.physicalDevice,
                                     sizeof(GpuLightsUBO),
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     true);
            if (!s.lightBuffers[i].valid())
            {
                std::cerr << "RendererVK: Failed to create Lights UBO for viewport frame " << i << ".\n";
                continue;
            }

            GpuLightsUBO zero = {};
            s.lightBuffers[i].upload(&zero, sizeof(zero));
            needWrite = true;
        }

        // ✅ ONLY write descriptors when something was created/recreated.
        if (needWrite)
        {
            s.uboSets[i].writeUniformBuffer(m_ctx.device, 0, s.mvpBuffers[i].buffer(), sizeof(MvpUBO));
            s.uboSets[i].writeUniformBuffer(m_ctx.device, 1, s.lightBuffers[i].buffer(), sizeof(GpuLightsUBO));
        }
    }

    return s;
}

//==================================================================
// RT per-viewport state (lazy allocation)
//==================================================================

Renderer::RtViewportState& Renderer::ensureRtViewportState(Viewport* vp)
{
    auto it = m_rtViewports.find(vp);
    if (it != m_rtViewports.end())
        return it->second;

    // Make sure the per-viewport raster UBO state exists too (we need lightBuffers).
    ViewportUboState& ubo = ensureViewportUboState(vp);

    RtViewportState st{};
    st.sets.resize(m_framesInFlight);
    st.cameraBuffers.resize(m_framesInFlight);
    st.instanceDataBuffers.resize(m_framesInFlight);
    st.images.resize(m_framesInFlight);

    st.scratchBuffers.resize(m_framesInFlight);
    st.scratchSizes.resize(m_framesInFlight, 0);

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

        // Write static bindings:
        st.sets[i].writeUniformBuffer(m_ctx.device, 2, st.cameraBuffers[i].buffer(), sizeof(RtCameraUBO));

        st.sets[i].writeStorageBuffer(m_ctx.device,
                                      4,
                                      st.instanceDataBuffers[i].buffer(),
                                      st.instanceDataBuffers[i].size(),
                                      0);

        // NEW: Lights UBO binding=5 (per viewport, per frame)
        if (i < ubo.lightBuffers.size() && ubo.lightBuffers[i].valid())
        {
            st.sets[i].writeUniformBuffer(m_ctx.device,
                                          5,
                                          ubo.lightBuffers[i].buffer(),
                                          sizeof(GpuLightsUBO));
        }

        // bindings 0/1 are written by ensureRtOutputImages(...)
        // binding 3 is written by writeRtTlasDescriptor(...)
    }

    auto [insIt, _] = m_rtViewports.emplace(vp, std::move(st));
    return insIt->second;
}

void Renderer::updateViewportLightsUbo(Viewport* vp, Scene* scene, uint32_t frameIndex)
{
    if (!vp || !scene)
        return;

    if (frameIndex >= m_framesInFlight)
        return;

    ViewportUboState& ubo = ensureViewportUboState(vp);

    if (frameIndex >= ubo.lightBuffers.size())
        return;

    if (!ubo.lightBuffers[frameIndex].valid())
        return;

    GpuLightsUBO lights{};
    buildGpuLightsUBO(m_headlight, *vp, scene, lights);
    ubo.lightBuffers[frameIndex].upload(&lights, sizeof(lights));
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

bool Renderer::ensureRtOutputImages(RtViewportState& s, const RenderFrameContext& fc, uint32_t w, uint32_t h)
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.physicalDevice)
        return false;

    if (w == 0 || h == 0)
        return false;

    const uint32_t frameIndex = fc.frameIndex;
    if (frameIndex >= m_framesInFlight)
        return false;

    if (s.sets.size() != m_framesInFlight || s.images.size() != m_framesInFlight)
        return false;

    // If THIS slot already matches, we're done.
    {
        const RtImagePerFrame& img = s.images[frameIndex];
        if (img.image && img.view && img.width == w && img.height == h)
        {
            s.cachedW = w;
            s.cachedH = h;
            return true;
        }
    }

    VkDevice device = m_ctx.device;

    // -------------------------------------------------------------------------
    // Destroy only this slot's resources (DEFERRED if available).
    // -------------------------------------------------------------------------
    {
        RtImagePerFrame& img = s.images[frameIndex];

        const VkImageView    oldView = img.view;
        const VkImage        oldImg  = img.image;
        const VkDeviceMemory oldMem  = img.memory;

        if (oldView || oldImg || oldMem)
        {
            auto destroyOld = [device, oldView, oldImg, oldMem]() noexcept {
                if (oldView)
                    vkDestroyImageView(device, oldView, nullptr);
                if (oldImg)
                    vkDestroyImage(device, oldImg, nullptr);
                if (oldMem)
                    vkFreeMemory(device, oldMem, nullptr);
            };

            if (fc.deferred)
                fc.deferred->enqueue(frameIndex, std::move(destroyOld));
            else
                destroyOld();
        }

        img.view   = VK_NULL_HANDLE;
        img.image  = VK_NULL_HANDLE;
        img.memory = VK_NULL_HANDLE;

        img.width     = 0;
        img.height    = 0;
        img.needsInit = true;
    }

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

    RtImagePerFrame newImg = {};

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

    if (vkCreateImage(device, &ici, nullptr, &newImg.image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, newImg.image, &req);

    const uint32_t typeIndex = findDeviceLocalType(req.memoryTypeBits);
    if (typeIndex == UINT32_MAX)
    {
        vkDestroyImage(device, newImg.image, nullptr);
        return false;
    }

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = typeIndex;

    if (vkAllocateMemory(device, &mai, nullptr, &newImg.memory) != VK_SUCCESS)
    {
        vkDestroyImage(device, newImg.image, nullptr);
        return false;
    }

    if (vkBindImageMemory(device, newImg.image, newImg.memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(device, newImg.memory, nullptr);
        vkDestroyImage(device, newImg.image, nullptr);
        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image                           = newImg.image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = m_rtFormat;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &vci, nullptr, &newImg.view) != VK_SUCCESS)
    {
        vkFreeMemory(device, newImg.memory, nullptr);
        vkDestroyImage(device, newImg.image, nullptr);
        return false;
    }

    newImg.width     = w;
    newImg.height    = h;
    newImg.needsInit = true;

    // Commit into this slot.
    s.images[frameIndex] = newImg;

    // Update only THIS frame slot’s descriptors (set=0 in RT)
    s.sets[frameIndex].writeStorageImage(device, 0, newImg.view, VK_IMAGE_LAYOUT_GENERAL);
    s.sets[frameIndex].writeCombinedImageSampler(device,
                                                 1,
                                                 m_rtSampler,
                                                 newImg.view,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    s.cachedW = w;
    s.cachedH = h;
    return true;
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

    m_materialSets[frameIndex].writeCombinedImageSamplerArray(device, 1, infos);
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

    DescriptorBindingInfo bindings[6] = {};

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
    bindings[2].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                         VK_SHADER_STAGE_MISS_BIT_KHR;
    bindings[2].count = 1;

    bindings[3].binding = 3;
    bindings[3].type    = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[3].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[3].count   = 1;

    bindings[4].binding = 4;
    bindings[4].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[4].count   = 1;

    // NEW: Lights UBO
    bindings[5].binding = 5;
    bindings[5].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[5].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; // add RGEN/RMISS later if needed
    bindings[5].count   = 1;

    if (!m_rtSetLayout.create(device, std::span{bindings, 6}))
    {
        std::cerr << "RendererVK: Failed to create RT DescriptorSetLayout.\n";
        return false;
    }

    // Pool is sized for (frames * maxViewports)
    const uint32_t setCount = std::max(1u, m_framesInFlight) * kMaxViewports;

    std::array<VkDescriptorPoolSize, 5> poolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount * 2}, // camera + lights
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

    // set=0 (rt), set=1 (materials)
    VkDescriptorSetLayout setLayouts[2] = {
        m_rtSetLayout.layout(),
        m_materialSetLayout.layout(),
    };

    if (!m_rtPipeline.createScenePipeline(m_ctx, setLayouts, 2))
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

bool Renderer::ensureRtScratch(Viewport* vp, const RenderFrameContext& fc, VkDeviceSize bytes) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !vp)
        return false;

    if (bytes == 0)
        return false;

    if (fc.frameIndex >= m_framesInFlight)
        return false;

    RtViewportState& rts = ensureRtViewportState(vp);

    if (rts.scratchBuffers.size() != m_framesInFlight)
    {
        rts.scratchBuffers.resize(m_framesInFlight);
        rts.scratchSizes.resize(m_framesInFlight, 0);
    }

    GpuBuffer&    scratch = rts.scratchBuffers[fc.frameIndex];
    VkDeviceSize& cap     = rts.scratchSizes[fc.frameIndex];

    // Vulkan requires scratch address alignment. We'll allocate extra so we can align the address safely.
    const VkDeviceSize align =
        (m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment != 0)
            ? VkDeviceSize(m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment)
            : VkDeviceSize(256);

    const VkDeviceSize want = bytes + align;

    if (scratch.valid() && cap >= want)
        return true;

    // If replacing an existing scratch buffer, defer destruction to this viewport’s per-frame queue.
    if (scratch.valid())
    {
        if (fc.deferred)
        {
            GpuBuffer old = std::move(scratch);
            cap           = 0;

            fc.deferred->enqueue(fc.frameIndex, [buf = std::move(old)]() mutable {
                buf.destroy();
            });
        }
        else
        {
            scratch.destroy();
            cap = 0;
        }
    }

    scratch.create(m_ctx.device,
                   m_ctx.physicalDevice,
                   want,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   false,
                   true);

    if (!scratch.valid())
        return false;

    cap = want;
    return true;
}

//==================================================================
// RT AS teardown (UNCHANGED from your current file)
//==================================================================

void Renderer::destroyRtBlasFor(SceneMesh* sm, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch)
        return;

    if (!sm)
        return;

    auto it = m_rtBlas.find(sm);
    if (it == m_rtBlas.end())
        return;

    RtBlas& b = it->second;

    // Nothing to destroy
    if (b.as == VK_NULL_HANDLE && !b.asBuffer.valid())
    {
        m_rtBlas.erase(it);
        return;
    }

    // Move resources out first so analyzers see a clear ownership transfer.
    VkAccelerationStructureKHR oldAs      = b.as;
    GpuBuffer                  oldBacking = std::move(b.asBuffer);

    // Clear the live entry immediately after moving out.
    // From here on, this RtBlas no longer owns the AS/backing buffer.
    b.as       = VK_NULL_HANDLE;
    b.asBuffer = {};

    b.address       = 0;
    b.buildKey      = 0;
    b.posBuffer     = VK_NULL_HANDLE;
    b.posCount      = 0;
    b.idxBuffer     = VK_NULL_HANDLE;
    b.idxCount      = 0;
    b.subdivLevel   = 0;
    b.topoCounter   = 0;
    b.deformCounter = 0;

    if (fc.deferred)
    {
        VkDevice device = m_ctx.device;
        auto*    rt     = m_ctx.rtDispatch;

        fc.deferred->enqueue(
            fc.frameIndex,
            [device, rt, oldAs, backing = std::move(oldBacking)]() mutable {
                if (rt && device && oldAs != VK_NULL_HANDLE)
                    rt->vkDestroyAccelerationStructureKHR(device, oldAs, nullptr);

                backing.destroy();
            });
    }
    else
    {
        // Fallback (ideally shouldn't happen)
        if (m_ctx.rtDispatch && m_ctx.device && oldAs != VK_NULL_HANDLE)
            m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, oldAs, nullptr);

        oldBacking.destroy();
    }

    // Remove entry from map now that resources have been scheduled/freed.
    m_rtBlas.erase(it); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
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

    // vkDeviceWaitIdle(m_ctx.device);

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

// void Renderer::clearRtTlasDescriptor(Viewport* vp, uint32_t frameIndex) noexcept
// {
//     if (!vp)
//         return;

//     RtViewportState& rtv = ensureRtViewportState(vp);
//     if (frameIndex >= rtv.sets.size())
//         return;

//     VkAccelerationStructureKHR nullAs = VK_NULL_HANDLE;

//     VkWriteDescriptorSetAccelerationStructureKHR asInfo{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
//     asInfo.accelerationStructureCount = 1;
//     asInfo.pAccelerationStructures    = &nullAs;

//     VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
//     w.pNext           = &asInfo;
//     w.dstSet          = rtv.sets[frameIndex].set();
//     w.dstBinding      = 3;
//     w.descriptorCount = 1;
//     w.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

//     vkUpdateDescriptorSets(m_ctx.device, 1, &w, 0, nullptr);
// }

void Renderer::clearRtTlasDescriptor(Viewport* /*vp*/, uint32_t /*frameIndex*/) noexcept
{
    // Intentionally does nothing unless nullDescriptor is enabled.
    //
    // Without nullDescriptor, writing VK_NULL_HANDLE to an
    // ACCELERATION_STRUCTURE_KHR descriptor is invalid and produces
    // validation errors.
    //
    // When TLAS is missing, the RT path exits before vkCmdTraceRaysKHR,
    // so the previous binding is never consumed by shaders.
}

void Renderer::destroyAllRtTlasFrames() noexcept
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_rtTlasFrames.size()); ++i)
        destroyRtTlasFrame(i, true);

    m_rtTlasFrames.clear();
}

//==================================================================
// renderPrePass (NOW does ALL MeshGpuResources::update(cmd) work here)
//==================================================================

void Renderer::renderPrePass(Viewport* vp, Scene* scene, const RenderFrameContext& fc)
{
    if (!vp || !scene || fc.cmd == VK_NULL_HANDLE)
        return;

    if (fc.frameIndex >= m_framesInFlight)
        return;

    // ------------------------------------------------------------
    // 1) Update ALL MeshGpuResources here (outside render pass)
    // ------------------------------------------------------------
    forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
        // IMPORTANT: update() records transfer/barrier commands.
        // Must be done BEFORE the render pass begins.
        gpu->update(fc);
    });

    // ------------------------------------------------------------
    // 2) RT dispatch (still here, also outside render pass)
    // ------------------------------------------------------------
    if (vp->drawMode() == DrawMode::RAY_TRACE)
    {
        if (!rtReady(m_ctx))
            return;

        renderRayTrace(vp, scene, fc);
    }
}

//==================================================================
// Render (NO MeshGpuResources::update(cmd) calls here anymore)
//==================================================================

//==================================================================
// Render (RT present + raster overlays/selection)
//==================================================================

void Renderer::render(Viewport* vp, Scene* scene, const RenderFrameContext& fc)
{
    if (!vp || !scene || fc.cmd == VK_NULL_HANDLE)
        return;

    if (fc.frameIndex >= m_framesInFlight)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    const VkCommandBuffer cmd      = fc.cmd;
    const uint32_t        frameIdx = fc.frameIndex;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());

    const glm::vec4 solidEdgeColor{0.10f, 0.10f, 0.10f, 0.5f};
    const glm::vec4 wireVisibleColor{0.85f, 0.85f, 0.85f, 1.0f};
    const glm::vec4 wireHiddenColor{0.85f, 0.85f, 0.85f, 0.25f};

    auto uploadViewportUboSet0 = [&]() -> bool {
        ViewportUboState& vpUbo = ensureViewportUboState(vp);

        if (frameIdx >= vpUbo.mvpBuffers.size() ||
            frameIdx >= vpUbo.lightBuffers.size() ||
            frameIdx >= vpUbo.uboSets.size())
        {
            return false;
        }

        if (!vpUbo.mvpBuffers[frameIdx].valid() ||
            !vpUbo.lightBuffers[frameIdx].valid())
        {
            return false;
        }

        // ------------------------------------------------------------
        // MVP
        // ------------------------------------------------------------
        {
            MvpUBO ubo{};
            ubo.proj = vp->projection();
            ubo.view = vp->view();
            vpUbo.mvpBuffers[frameIdx].upload(&ubo, sizeof(ubo));
        }

        // ------------------------------------------------------------
        // Lights (THIS WAS MISSING)
        // ------------------------------------------------------------
        {
            GpuLightsUBO lights{};
            buildGpuLightsUBO(
                m_headlight, // Renderer-owned modeling light
                *vp,
                scene,
                lights);

            vpUbo.lightBuffers[frameIdx].upload(&lights, sizeof(lights));
        }

        // ------------------------------------------------------------
        // Bind set=0 (MVP + Lights)
        // ------------------------------------------------------------
        {
            VkDescriptorSet gfxSet0 = vpUbo.uboSets[frameIdx].set();
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout,
                                    0,
                                    1,
                                    &gfxSet0,
                                    0,
                                    nullptr);
        }

        return true;
    };

    // ------------------------------------------------------------
    // RAY TRACE PRESENT PATH (present RT image, then draw overlays)
    // ------------------------------------------------------------
    if (vp->drawMode() == DrawMode::RAY_TRACE)
    {
        if (!rtReady(m_ctx))
            return;

        RtViewportState& rtv = ensureRtViewportState(vp);

        if (!ensureRtOutputImages(rtv, fc, w, h))
            return;

        if (!m_rtPresentPipeline || !m_rtPresentLayout)
            return;

        if (frameIdx >= rtv.sets.size())
            return;

        // Present RT output
        vkutil::setViewportAndScissor(cmd, w, h);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rtPresentPipeline);

        VkDescriptorSet rtSet0 = rtv.sets[frameIdx].set();
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_rtPresentLayout,
                                0,
                                1,
                                &rtSet0,
                                0,
                                nullptr);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        // Restore normal set=0 (MVP + Lights) so overlays/selection/grid can render on top.
        if (!uploadViewportUboSet0())
            return;

        vkutil::setViewportAndScissor(cmd, w, h);

        drawSelection(cmd, vp, scene);

        if (scene->showSceneGrid() && vp->drawMode() != DrawMode::SHADED)
        {
            drawSceneGrid(cmd, vp, scene);
        }

        return;
    }

    // ------------------------------------------------------------
    // NORMAL GRAPHICS PATH (bind MVP+Lights set=0)
    // ------------------------------------------------------------
    if (!uploadViewportUboSet0())
        return;

    vkutil::setViewportAndScissor(cmd, w, h);

    // ------------------------------------------------------------
    // Solid / Shaded
    // ------------------------------------------------------------
    if (vp->drawMode() != DrawMode::WIREFRAME)
    {
        const bool isShaded = (vp->drawMode() == DrawMode::SHADED);

        VkPipeline triPipe = isShaded ? m_pipelineShaded : m_pipelineSolid;

        if (scene->materialHandler() &&
            m_curMaterialCounter != scene->materialHandler()->changeCounter()->value())
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
            VkDescriptorSet set1 = m_materialSets[frameIdx].set();
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

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 1);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                const render::geom::GfxMeshGeometry geo = render::geom::selectGfxGeometry(sm, gpu);
                if (!geo.valid())
                    return;

                VkBuffer     bufs[4] = {geo.posBuffer, geo.nrmBuffer, geo.uvBuffer, geo.matBuffer};
                VkDeviceSize offs[4] = {0, 0, 0, 0};

                vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                vkCmdDraw(cmd, geo.vertexCount, 1, 0, 0);
            });
        }

        constexpr bool drawEdgesInSolid = true;
        if (!isShaded && drawEdgesInSolid && m_pipelineEdgeDepthBias)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineEdgeDepthBias);

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
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

                const render::geom::WireDrawGeo wgeo = render::geom::selectWireGeometry(gpu, useSubdiv);
                if (!wgeo.valid())
                    return;

                VkDeviceSize voff = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &wgeo.posVb, &voff);
                vkCmdBindIndexBuffer(cmd, wgeo.idxIb, 0, wgeo.idxType);
                vkCmdDrawIndexed(cmd, wgeo.idxCount, 1, 0, 0, 0);
            });
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

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
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

                const render::geom::GfxMeshGeometry geo = render::geom::selectGfxGeometry(sm, gpu);
                if (!geo.valid())
                    return;

                VkBuffer     bufs[4] = {geo.posBuffer, geo.nrmBuffer, geo.uvBuffer, geo.matBuffer};
                VkDeviceSize offs[4] = {0, 0, 0, 0};

                vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                vkCmdDraw(cmd, geo.vertexCount, 1, 0, 0);
            });
        }

        auto drawEdges = [&](VkPipeline pipeline, const glm::vec4& color) {
            if (!pipeline)
                return;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
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

                const render::geom::WireDrawGeo wgeo = render::geom::selectWireGeometry(gpu, useSubdiv);
                if (!wgeo.valid())
                    return;

                VkDeviceSize voff = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &wgeo.posVb, &voff);
                vkCmdBindIndexBuffer(cmd, wgeo.idxIb, 0, wgeo.idxType);
                vkCmdDrawIndexed(cmd, wgeo.idxCount, 1, 0, 0, 0);
            });
        };

        drawEdges(m_pipelineEdgeHidden, wireHiddenColor);
        drawEdges(m_pipelineWire, wireVisibleColor);
    }

    drawSelection(cmd, vp, scene);

    if (scene->showSceneGrid() && vp->drawMode() != DrawMode::SHADED)
    {
        drawSceneGrid(cmd, vp, scene);
    }
}

//==================================================================
// RT dispatch (per-viewport)
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

    rtv.sets[frameIndex].writeAccelerationStructure(m_ctx.device, 3, tf.as);
}

//==================================================================
// RT dispatch
//==================================================================

void Renderer::renderRayTrace(Viewport* vp, Scene* scene, const RenderFrameContext& fc)
{
    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return;

    if (!fc.cmd || !vp || !scene)
        return;

    if (!m_rtPipeline.valid() || !m_rtSbt.buffer())
        return;

    if (fc.frameIndex >= m_framesInFlight)
        return;

    // Ensure per-viewport lighting buffers exist and contain current data
    updateViewportLightsUbo(vp, scene, fc.frameIndex);

    ViewportUboState& ubo = ensureViewportUboState(vp);
    RtViewportState&  rtv = ensureRtViewportState(vp);

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());
    if (w == 0 || h == 0)
        return;

    if (!ensureRtOutputImages(rtv, fc, w, h))
        return;

    if (fc.frameIndex >= rtv.images.size() ||
        fc.frameIndex >= rtv.cameraBuffers.size() ||
        fc.frameIndex >= rtv.instanceDataBuffers.size() ||
        fc.frameIndex >= rtv.sets.size())
    {
        return;
    }

    RtImagePerFrame& out = rtv.images[fc.frameIndex];
    if (!out.image || !out.view)
        return;

    // ------------------------------------------------------------
    // Bind lights UBO (binding=5) for this frame (refresh-safe)
    // ------------------------------------------------------------
    // if (fc.frameIndex < ubo.lightBuffers.size() && ubo.lightBuffers[fc.frameIndex].valid())  // done already in ensureRtViewportState
    // {
    //     rtv.sets[fc.frameIndex].writeUniformBuffer(m_ctx.device,
    //                                                5,
    //                                                ubo.lightBuffers[fc.frameIndex].buffer(),
    //                                                sizeof(GpuLightsUBO));
    // }

    // ------------------------------------------------------------
    // Clear RT output to viewport background (safe even if no TLAS)
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
            vkutil::imageBarrier(fc.cmd,
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
            vkutil::imageBarrier(fc.cmd,
                                 out.image,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 VK_ACCESS_SHADER_READ_BIT,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        vkCmdClearColorImage(fc.cmd, out.image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);

        vkutil::imageBarrier(fc.cmd,
                             out.image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_SHADER_READ_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    // ------------------------------------------------------------
    // Build (or DESTROY) BLAS for visible meshes
    // ------------------------------------------------------------
    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        const render::geom::RtMeshGeometry geo = render::geom::selectRtGeometry(sm);

        if (!geo.valid() || geo.buildIndexCount == 0 || geo.buildPosCount == 0)
        {
            destroyRtBlasFor(sm, fc);
            continue;
        }

        (void)ensureMeshBlas(vp, sm, geo, fc);
    }

    // ------------------------------------------------------------
    // Ensure scene TLAS
    // ------------------------------------------------------------
    if (!ensureSceneTlas(vp, scene, fc))
        return;

    if (fc.frameIndex >= m_rtTlasFrames.size() ||
        m_rtTlasFrames[fc.frameIndex].as == VK_NULL_HANDLE)
    {
        // Avoid writing NULL AS unless nullDescriptor is enabled:
        // just keep cleared output and bail.
        return;
    }

    // Bind TLAS into THIS viewport’s RT set for this frame
    writeRtTlasDescriptor(vp, fc.frameIndex);

    // ------------------------------------------------------------
    // Upload per-instance shader data
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

            const render::geom::RtMeshGeometry geo = render::geom::selectRtGeometry(sm);
            if (!geo.valid() || !geo.shaderValid())
                continue;

            const uint32_t primCount = geo.buildIndexCount / 3u;
            if (primCount == 0)
                continue;

            if (geo.shaderTriCount != primCount)
                continue;

            if (geo.shadeNrmCount != primCount * 3u)
                continue;

            if (geo.shadeUvCount != primCount * 3u)
                continue;

            if (geo.shadeMatIdCount != primCount)
                continue;

            RtInstanceData d = {};
            d.posAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadePosBuffer);
            d.idxAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shaderIndexBuffer);
            d.nrmAdr         = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeNrmBuffer);
            d.uvAdr          = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeUvBuffer);
            d.matIdAdr       = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeMatIdBuffer);
            d.triCount       = geo.shaderTriCount;

            if (d.posAdr == 0 || d.idxAdr == 0 || d.nrmAdr == 0 || d.uvAdr == 0 || d.matIdAdr == 0 || d.triCount == 0)
                continue;

            instData.push_back(d);
        }

        if (!instData.empty())
        {
            const VkDeviceSize bytes = VkDeviceSize(instData.size() * sizeof(RtInstanceData));

            rtv.instanceDataBuffers[fc.frameIndex].upload(instData.data(), bytes);

            rtv.sets[fc.frameIndex].writeStorageBuffer(m_ctx.device,
                                                       4,
                                                       rtv.instanceDataBuffers[fc.frameIndex].buffer(),
                                                       bytes,
                                                       0);
        }
        else
        {
            rtv.sets[fc.frameIndex].writeStorageBuffer(m_ctx.device,
                                                       4,
                                                       rtv.instanceDataBuffers[fc.frameIndex].buffer(),
                                                       0,
                                                       0);
        }
    }

    // ------------------------------------------------------------
    // Update RT camera UBO
    // ------------------------------------------------------------
    {
        RtCameraUBO cam = {};
        cam.invViewProj = glm::inverse(vp->projection() * vp->view());
        cam.view        = vp->view();
        cam.camPos      = glm::vec4(vp->cameraPosition(), 1.0f);
        cam.clearColor  = vp->clearColor();
        rtv.cameraBuffers[fc.frameIndex].upload(&cam, sizeof(cam));
    }

    // ------------------------------------------------------------
    // Ensure material table is up to date for RT
    // ------------------------------------------------------------
    if (scene->materialHandler() &&
        m_curMaterialCounter != scene->materialHandler()->changeCounter()->value())
    {
        const auto& mats = scene->materialHandler()->materials();
        for (uint32_t i = 0; i < m_ctx.framesInFlight; ++i)
        {
            uploadMaterialsToGpu(mats, *scene->textureHandler(), i);
            updateMaterialTextureTable(*scene->textureHandler(), i);
        }
        m_curMaterialCounter = scene->materialHandler()->changeCounter()->value();
    }

    // ------------------------------------------------------------
    // Transition for raygen writes
    // ------------------------------------------------------------
    vkutil::imageBarrier(fc.cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    // ------------------------------------------------------------
    // Bind RT pipeline + descriptor sets
    // ------------------------------------------------------------
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline.pipeline());

    VkDescriptorSet sets[2] = {
        rtv.sets[fc.frameIndex].set(),      // set 0 : RT (includes binding 5 now)
        m_materialSets[fc.frameIndex].set() // set 1 : materials
    };

    vkCmdBindDescriptorSets(fc.cmd,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_rtPipeline.layout(),
                            0,
                            2,
                            sets,
                            0,
                            nullptr);

    VkStridedDeviceAddressRegionKHR rgen = {};
    VkStridedDeviceAddressRegionKHR miss = {};
    VkStridedDeviceAddressRegionKHR hit  = {};
    VkStridedDeviceAddressRegionKHR call = {};
    m_rtSbt.regions(m_ctx, rgen, miss, hit, call);

    m_ctx.rtDispatch->vkCmdTraceRaysKHR(fc.cmd, &rgen, &miss, &hit, &call, w, h, 1);

    // ------------------------------------------------------------
    // Transition back for present sampling
    // ------------------------------------------------------------
    vkutil::imageBarrier(fc.cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

bool Renderer::ensureMeshBlas(Viewport* vp, SceneMesh* sm, const render::geom::RtMeshGeometry& geo, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !vp || !sm || !fc.cmd)
        return false;

    if (!geo.valid() || geo.buildIndexCount == 0 || geo.buildPosCount == 0)
        return false;

    if (fc.frameIndex >= m_framesInFlight)
        return false;

    // ------------------------------------------------------------
    // Build key: topology+deform counters + geometry sizes.
    // ------------------------------------------------------------
    uint64_t topo   = 0;
    uint64_t deform = 0;

    if (sm->sysMesh())
    {
        if (sm->sysMesh()->topology_counter())
            topo = sm->sysMesh()->topology_counter()->value();
        if (sm->sysMesh()->deform_counter())
            deform = sm->sysMesh()->deform_counter()->value();
    }

    uint64_t key = topo;
    key ^= (deform + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
    key ^= (uint64_t(geo.buildPosCount) << 32) ^ uint64_t(geo.buildIndexCount);

    RtBlas& b = m_rtBlas[sm];

    if (b.as != VK_NULL_HANDLE && b.buildKey == key)
        return true;

    // ------------------------------------------------------------
    // Tear down existing BLAS (deferred to the *viewport* frame slot)
    // ------------------------------------------------------------
    if (b.as != VK_NULL_HANDLE || b.asBuffer.valid())
    {
        if (fc.deferred)
        {
            VkDevice device = m_ctx.device;
            auto*    rt     = m_ctx.rtDispatch;

            VkAccelerationStructureKHR oldAs      = b.as;
            GpuBuffer                  oldBacking = std::move(b.asBuffer);

            fc.deferred->enqueue(fc.frameIndex,
                                 [device, rt, oldAs, backing = std::move(oldBacking)]() mutable {
                                     if (rt && device && oldAs != VK_NULL_HANDLE)
                                         rt->vkDestroyAccelerationStructureKHR(device, oldAs, nullptr);

                                     backing.destroy();
                                 });
        }
        else
        {
            if (m_ctx.rtDispatch && m_ctx.device && b.as != VK_NULL_HANDLE)
                m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);

            b.asBuffer.destroy();
        }

        b.as       = VK_NULL_HANDLE; // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        b.asBuffer = {};
    }

    b.address  = 0;
    b.buildKey = 0;

    // ------------------------------------------------------------
    // Geometry device addresses
    // ------------------------------------------------------------
    const VkDeviceAddress vAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildPosBuffer);
    const VkDeviceAddress iAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildIndexBuffer);

    if (vAdr == 0 || iAdr == 0)
        return false;

    VkAccelerationStructureGeometryKHR asGeom = {};
    asGeom.sType                              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeom.geometryType                       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeom.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureGeometryTrianglesDataKHR tri = {};
    tri.sType                                           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat                                    = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress                        = vAdr;
    tri.vertexStride                                    = sizeof(glm::vec3);
    tri.maxVertex                                       = geo.buildPosCount ? (geo.buildPosCount - 1) : 0;
    tri.indexType                                       = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress                         = iAdr;

    asGeom.geometry.triangles = tri;

    const uint32_t primCount = geo.buildIndexCount / 3u;
    if (primCount == 0)
        return false;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
    buildInfo.sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type                                        = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags                                       = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode                                        = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount                               = 1;
    buildInfo.pGeometries                                 = &asGeom;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
    sizeInfo.sType                                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    auto* rt = m_ctx.rtDispatch;
    if (!rt)
        return false;

    rt->vkGetAccelerationStructureBuildSizesKHR(m_ctx.device,
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &buildInfo,
                                                &primCount,
                                                &sizeInfo);

    if (sizeInfo.accelerationStructureSize == 0 || sizeInfo.buildScratchSize == 0)
        return false;

    // ------------------------------------------------------------
    // Create buffer backing the BLAS
    // ------------------------------------------------------------
    b.asBuffer.create(m_ctx.device,
                      m_ctx.physicalDevice,
                      sizeInfo.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      false,
                      true);

    if (!b.asBuffer.valid())
        return false;

    VkAccelerationStructureCreateInfoKHR asci = {};
    asci.sType                                = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asci.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asci.size                                 = sizeInfo.accelerationStructureSize;
    asci.buffer                               = b.asBuffer.buffer();

    if (m_ctx.rtDispatch->vkCreateAccelerationStructureKHR(m_ctx.device, &asci, nullptr, &b.as) != VK_SUCCESS)
        return false;

    // ------------------------------------------------------------
    // Per-viewport per-frame scratch (CRITICAL for multi-viewport)
    // ------------------------------------------------------------
    if (!ensureRtScratch(vp, fc, sizeInfo.buildScratchSize))
        return false;

    RtViewportState& rts     = ensureRtViewportState(vp);
    GpuBuffer&       scratch = rts.scratchBuffers[fc.frameIndex];

    VkDeviceAddress scratchAdr = vkutil::bufferDeviceAddress(m_ctx.device, scratch.buffer());
    if (scratchAdr == 0)
        return false;

    const VkDeviceSize align =
        (m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment != 0)
            ? VkDeviceSize(m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment)
            : VkDeviceSize(256);

    scratchAdr = vkrt::align_up<VkDeviceAddress>(scratchAdr, VkDeviceAddress(align));

    buildInfo.dstAccelerationStructure  = b.as;
    buildInfo.scratchData.deviceAddress = scratchAdr;

    VkAccelerationStructureBuildRangeInfoKHR range            = {};
    range.primitiveCount                                      = primCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRanges[] = {&range};

    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(fc.cmd, 1, &buildInfo, pRanges);

    // Barrier: BLAS build writes -> RT reads
    vkutil::barrierAsBuildToTrace(fc.cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {};
    addrInfo.sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure                       = b.as;

    b.address  = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    b.buildKey = key;

    return (b.address != 0);
}

bool Renderer::ensureSceneTlas(Viewport* vp, Scene* scene, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !scene || !fc.cmd || !vp)
        return false;

    if (fc.frameIndex >= m_rtTlasFrames.size())
        return false;

    RtTlasFrame& t = m_rtTlasFrames[fc.frameIndex];

    // Use your change counter as the TLAS rebuild key.
    uint64_t key = m_rtTlasChangeCounter ? m_rtTlasChangeCounter->value() : 1ull;

    // ------------------------------------------------------------
    // Helper: iterate visible meshes that have a valid BLAS (no GPU creation!)
    // ------------------------------------------------------------
    auto forEachVisibleBlasMesh = [&](auto&& fn) {
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

            fn(*sm, b);
        }
    };

    // Also rebuild TLAS if any BLAS changed
    forEachVisibleBlasMesh([&](SceneMesh& /*sm*/, const RtBlas& b) {
        key ^= (b.buildKey + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
    });

    if (t.as != VK_NULL_HANDLE && t.buildKey == key)
        return true;

    // ------------------------------------------------------------
    // Gather instances (must match ordering used for RtInstanceData upload!)
    // ------------------------------------------------------------
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(scene->sceneMeshes().size());

    forEachVisibleBlasMesh([&](SceneMesh& /*sm*/, const RtBlas& b) {
        VkAccelerationStructureInstanceKHR inst{};
        inst.transform.matrix[0][0] = 1.0f;
        inst.transform.matrix[1][1] = 1.0f;
        inst.transform.matrix[2][2] = 1.0f;

        inst.instanceCustomIndex                    = static_cast<uint32_t>(instances.size());
        inst.mask                                   = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference         = b.address;

        instances.push_back(inst);
    });

    if (instances.empty())
    {
        // No geometry: destroy TLAS if it exists.
        destroyRtTlasFrame(fc.frameIndex, false);

        // CRITICAL: clear stale TLAS binding for this viewport+frame
        clearRtTlasDescriptor(vp, fc.frameIndex);

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
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
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
    vkCmdCopyBuffer(fc.cmd, t.instanceStaging.buffer(), t.instanceBuffer.buffer(), 1, &cpy);

    // Barrier: transfer write -> AS build read
    vkutil::barrierTransferToAsBuildRead(fc.cmd);

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
        if (t.as != VK_NULL_HANDLE || t.buffer.valid())
        {
            VkAccelerationStructureKHR oldAs      = std::exchange(t.as, VK_NULL_HANDLE);
            GpuBuffer                  oldBacking = std::exchange(t.buffer, {});

            if (fc.deferred)
            {
                VkDevice device = m_ctx.device;
                auto*    rt     = m_ctx.rtDispatch;

                fc.deferred->enqueue(
                    fc.frameIndex,
                    [device, rt, oldAs, backing = std::move(oldBacking)]() mutable {
                        if (rt && device && oldAs != VK_NULL_HANDLE)
                            rt->vkDestroyAccelerationStructureKHR(device, oldAs, nullptr);
                        backing.destroy();
                    });
            }
            else
            {
                if (m_ctx.rtDispatch && m_ctx.device && oldAs)
                    m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, oldAs, nullptr);

                oldBacking.destroy();
            }
        }

        t.address = 0; // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

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

    if (!ensureRtScratch(vp, fc, sizeInfo.buildScratchSize))
        return false;

    RtViewportState& rts     = ensureRtViewportState(vp);
    GpuBuffer&       scratch = rts.scratchBuffers[fc.frameIndex];

    VkDeviceAddress scratchAdr = vkutil::bufferDeviceAddress(m_ctx.device, scratch.buffer());
    if (scratchAdr == 0)
        return false;

    const VkDeviceSize align =
        (m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment != 0)
            ? VkDeviceSize(m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment)
            : VkDeviceSize(256);

    scratchAdr = vkrt::align_up<VkDeviceAddress>(scratchAdr, VkDeviceAddress(align));

    buildInfo.dstAccelerationStructure  = t.as;
    buildInfo.scratchData.deviceAddress = scratchAdr;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount                                      = primCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRanges[] = {&range};

    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(fc.cmd, 1, &buildInfo, pRanges);

    // Barrier: TLAS build writes -> RT reads
    vkutil::barrierAsBuildToTrace(fc.cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = t.as;

    t.address  = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    t.buildKey = key;

    return (t.address != 0);
}

//==================================================================
// drawOverlays / drawSelection / drawSceneGrid / ensureOverlayVertexCapacity
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

//==================================================================
// drawSelection (NO MeshGpuResources::update(cmd) calls here)
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

    auto pushPC = [&](SceneMesh& sm, const glm::vec4& color) {
        PushConstants pc = {};
        pc.model         = sm.model();
        pc.color         = color;

        vkCmdPushConstants(cmd,
                           m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(PushConstants),
                           &pc);
    };

    auto drawHidden = [&](SceneMesh& sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!showOccluded)
            return;
        if (!pipeline || indexCount == 0)
            return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
        pushPC(sm, selColorHidden);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    auto drawVisible = [&](SceneMesh& sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!pipeline || indexCount == 0)
            return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetDepthBias(cmd, -1.0f, 0.0f, -1.0f);
        pushPC(sm, selColorVisible);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    const render::geom::SelPipelines pipes{
        .vertVis = m_pipelineSelVert,
        .vertHid = m_pipelineSelVertHidden,
        .edgeVis = m_pipelineSelEdge,
        .edgeHid = m_pipelineSelEdgeHidden,
        .polyVis = m_pipelineSelPoly,
        .polyHid = m_pipelineSelPolyHidden,
    };

    const SelectionMode mode = scene->selectionMode();

    forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
        const bool useSubdiv = (sm->subdivisionLevel() > 0);

        const render::geom::SelDrawGeo geo =
            render::geom::selectSelGeometry(gpu, useSubdiv, mode, pipes);

        if (!geo.valid())
            return;

        vkCmdBindVertexBuffers(cmd, 0, 1, &geo.posVb, &zeroOffset);
        vkCmdBindIndexBuffer(cmd, geo.selIb, 0, VK_INDEX_TYPE_UINT32);

        drawHidden(*sm, geo.pipeHid, geo.selCount);
        drawVisible(*sm, geo.pipeVis, geo.selCount);
    });

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

    glm::mat4 gridModel = render::geom::gridModelFor(vp->viewMode());

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

template<typename Fn>
void Renderer::forEachVisibleMesh(Scene* scene, Fn&& fn)
{
    if (!scene)
        return;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        if (!sm->gpu())
            sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

        MeshGpuResources* gpu = sm->gpu();
        if (!gpu)
            continue;

        fn(sm, gpu);
    }
}
