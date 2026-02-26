#include "RtRenderer.hpp"

#include <SysMesh.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

#include "RenderGeometry.hpp"
#include "RtPipeline.hpp"
#include "RtPresentPipeline.hpp"
#include "RtSbt.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "Viewport.hpp"
#include "VkUtilities.hpp"

RtRenderer::RtRenderer() noexcept
    : m_tlasChangeCounter(std::make_shared<SysCounter>()),
      m_tlasChangeMonitor(m_tlasChangeCounter)
{
}

//==================================================================
// RtViewportState destruction
//==================================================================

void RtRenderer::RtViewportState::destroyDeviceResources(const VulkanContext& ctx, uint32_t framesInFlight) noexcept
{
    const uint32_t fi = std::min(framesInFlight, vkcfg::kMaxFramesInFlight);

    for (uint32_t i = 0; i < fi; ++i)
    {
        instanceDataBuffers[i].destroy();
        instanceDataBuffers[i] = {};

        scratchBuffers[i].destroy();
        scratchBuffers[i] = {};
        scratchSizes[i]   = 0;

        // DescriptorSet is pool-owned (handle wrapper).
        sets[i] = {};
    }

    if (ctx.device)
    {
        VkDevice device = ctx.device;

        for (uint32_t i = 0; i < fi; ++i)
        {
            RtImagePerFrame& img = images[i];

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

    cachedW = 0;
    cachedH = 0;
}

//==================================================================
// Lifetime
//==================================================================

bool RtRenderer::initDevice(const VulkanContext& ctx, VkDescriptorSetLayout set0Layout, VkDescriptorSetLayout set1Layout)
{
    m_ctx = ctx;

    if (!m_ctx.device)
        return false;

    // If RT is not ready/available, succeed but stay inert.
    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return true;

    m_framesInFlight = std::max(1u, std::min(ctx.framesInFlight, vkcfg::kMaxFramesInFlight));

    // ------------------------------------------------------------
    // Set 2 = RT-only
    //   binding 0 = storage image
    //   binding 1 = combined image sampler
    //   binding 2 = TLAS
    //   binding 3 = instance data SSBO
    // ------------------------------------------------------------
    DescriptorBindingInfo bindings[4]{};

    bindings[0].binding = 0;
    bindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[0].count   = 1;

    bindings[1].binding = 1;
    bindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[1].count   = 1;

    bindings[2].binding = 2;
    bindings[2].type    = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[2].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[2].count   = 1;

    bindings[3].binding = 3;
    bindings[3].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    bindings[3].count   = 1;

    m_rtSetLayout.destroy();
    if (!m_rtSetLayout.create(m_ctx.device, std::span{bindings, 4}))
    {
        std::cerr << "RtRenderer: Failed to create RT DescriptorSetLayout.\n";
        return false;
    }

    // Pool sized for (frames * maxViewports). If you don't have a max, this is a safe upper bound.
    constexpr uint32_t kMaxViewports = 8;
    const uint32_t     setCount      = m_framesInFlight * kMaxViewports;

    std::array<VkDescriptorPoolSize, 4> poolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount},
    };

    m_rtPool.destroy();
    if (!m_rtPool.create(m_ctx.device, poolSizes, setCount))
    {
        std::cerr << "RtRenderer: Failed to create RT DescriptorPool.\n";
        return false;
    }

    // Sampler for present (and optional raygen sampling)
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

        if (vkCreateSampler(m_ctx.device, &sci, nullptr, &m_rtSampler) != VK_SUCCESS)
        {
            std::cerr << "RtRenderer: Failed to create RT sampler.\n";
            return false;
        }
    }

    // RT pipeline layout: set0 + set1 + set2
    VkDescriptorSetLayout setLayouts[3] = {set0Layout, set1Layout, m_rtSetLayout.layout()};

    m_rtPipeline.destroy();
    if (!m_rtPipeline.createScenePipeline(m_ctx, setLayouts, 3))
    {
        std::cerr << "RtRenderer: Failed to create RT scene pipeline.\n";
        return false;
    }

    // Upload pool for SBT build
    if (m_rtUploadPool == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_ctx.graphicsQueueFamilyIndex;

        if (vkCreateCommandPool(m_ctx.device, &pci, nullptr, &m_rtUploadPool) != VK_SUCCESS)
        {
            std::cerr << "RtRenderer: Failed to create RT upload command pool.\n";
            return false;
        }
    }

    // Build/upload SBT
    if (!m_rtSbt.buildAndUpload(m_ctx,
                                m_rtPipeline.pipeline(),
                                vkrt::RtPipeline::kRaygenCount,
                                vkrt::RtPipeline::kMissCount,
                                vkrt::RtPipeline::kHitCount,
                                vkrt::RtPipeline::kCallableCount,
                                m_rtUploadPool,
                                m_ctx.graphicsQueue))
    {
        std::cerr << "RtRenderer: Failed to build/upload SBT.\n";
        return false;
    }

    return true;
}

void RtRenderer::shutdown() noexcept
{
    // Per-viewport RT state
    for (auto& [vp, st] : m_viewports)
    {
        (void)vp;
        st.destroyDeviceResources(m_ctx, m_framesInFlight);
    }
    m_viewports.clear();

    // Device-level RT resources
    destroyAllRtTlasFrames();
    destroyAllRtBlas();

    m_rtPresent.destroy(m_ctx.device);

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

    m_ctx            = {};
    m_framesInFlight = 1;
}

//==================================================================
// Swapchain lifetime
//==================================================================

bool RtRenderer::initSwapchain(VkRenderPass renderPass)
{
    if (!m_ctx.device)
        return false;

    m_rtPresent.destroy(m_ctx.device);

    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return true;

    if (!m_rtSetLayout.layout())
    {
        std::cerr << "RtRenderer: RT set layout missing.\n";
        return false;
    }

    if (!m_rtPresent.create(m_ctx.device, renderPass, m_ctx.sampleCount, m_rtSetLayout.layout()))
    {
        std::cerr << "RtRenderer: RtPresentPipeline::create() failed.\n";
        return false;
    }

    return true;
}

void RtRenderer::destroySwapchainResources() noexcept
{
    if (!m_ctx.device)
        return;

    m_rtPresent.destroy(m_ctx.device);
}

//==================================================================
// Per-viewport RT state (lazy allocation)
//==================================================================

RtRenderer::RtViewportState& RtRenderer::ensureViewportState(Viewport* vp, uint32_t frameIndex)
{
    RtViewportState& st = m_viewports[vp];

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (frameIndex >= fi)
        return st;

    bool needWrite = false;

    if (st.sets[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!st.sets[frameIndex].allocate(m_ctx.device, m_rtPool.pool(), m_rtSetLayout.layout()))
        {
            std::cerr << "RtRenderer: Failed to allocate RT set for viewport frame " << frameIndex << ".\n";
            return st;
        }
        needWrite = true;
    }

    if (!st.instanceDataBuffers[frameIndex].valid())
    {
        st.instanceDataBuffers[frameIndex].create(m_ctx.device,
                                                  m_ctx.physicalDevice,
                                                  sizeof(RtInstanceData),
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                  false,
                                                  false);
        needWrite = true;
    }

    if (needWrite && st.instanceDataBuffers[frameIndex].valid())
    {
        st.sets[frameIndex].writeStorageBuffer(m_ctx.device,
                                               3,
                                               st.instanceDataBuffers[frameIndex].buffer(),
                                               st.instanceDataBuffers[frameIndex].size(),
                                               0);
    }

    return st;
}

//==================================================================
// RT output images
//==================================================================

bool RtRenderer::ensureRtOutputImages(RtViewportState& s, const RenderFrameContext& fc, uint32_t w, uint32_t h)
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.physicalDevice)
        return false;

    if (w == 0 || h == 0)
        return false;

    const uint32_t frameIndex = fc.frameIndex;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (frameIndex >= fi)
        return false;

    // Already matches
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

    // Destroy slot resources (deferred if available)
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

        img.view      = VK_NULL_HANDLE;
        img.image     = VK_NULL_HANDLE;
        img.memory    = VK_NULL_HANDLE;
        img.width     = 0;
        img.height    = 0;
        img.needsInit = true;
    }

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_ctx.physicalDevice, &memProps);

    auto findDeviceLocalType = [&](uint32_t typeBits) noexcept -> uint32_t {
        for (uint32_t m = 0; m < memProps.memoryTypeCount; ++m)
        {
            if ((typeBits & (1u << m)) && (memProps.memoryTypes[m].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                return m;
        }
        return UINT32_MAX;
    };

    RtImagePerFrame newImg = {};

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = m_rtFormat;
    ici.extent        = VkExtent3D{w, h, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

    s.images[frameIndex] = newImg;

    // Set 2 updates (image descriptors)
    writeRtImageDescriptors(s, frameIndex);

    s.cachedW = w;
    s.cachedH = h;
    return true;
}

void RtRenderer::writeRtImageDescriptors(RtViewportState& s, uint32_t frameIndex) noexcept
{
    if (!m_ctx.device)
        return;

    RtImagePerFrame& img = s.images[frameIndex];
    if (!img.view)
        return;

    // binding 0: storage image (raygen writes)
    s.sets[frameIndex].writeStorageImage(m_ctx.device, 0, img.view, VK_IMAGE_LAYOUT_GENERAL);

    // binding 1: combined image sampler (present samples)
    s.sets[frameIndex].writeCombinedImageSampler(m_ctx.device,
                                                 1,
                                                 m_rtSampler,
                                                 img.view,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

//==================================================================
// RT scratch
//==================================================================

bool RtRenderer::ensureRtScratch(RtViewportState& s, const RenderFrameContext& fc, VkDeviceSize bytes) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device)
        return false;

    if (bytes == 0)
        return false;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (fc.frameIndex >= fi)
        return false;

    GpuBuffer&    scratch = s.scratchBuffers[fc.frameIndex];
    VkDeviceSize& cap     = s.scratchSizes[fc.frameIndex];

    const VkDeviceSize align =
        (m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment != 0)
            ? VkDeviceSize(m_ctx.asProps.minAccelerationStructureScratchOffsetAlignment)
            : VkDeviceSize(256);

    const VkDeviceSize want = bytes + align;

    if (scratch.valid() && cap >= want)
        return true;

    if (scratch.valid())
    {
        if (fc.deferred)
        {
            GpuBuffer old = std::move(scratch);
            cap           = 0;
            fc.deferred->enqueue(fc.frameIndex, [buf = std::move(old)]() mutable { buf.destroy(); });
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
// RT AS teardown
//==================================================================

void RtRenderer::destroyRtBlasFor(SceneMesh* sm, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch)
        return;

    if (!sm)
        return;

    auto it = m_blas.find(sm);
    if (it == m_blas.end())
        return;

    RtBlas& b = it->second;

    if (b.as == VK_NULL_HANDLE && !b.asBuffer.valid())
    {
        m_blas.erase(it);
        return;
    }

    VkAccelerationStructureKHR oldAs      = b.as;
    GpuBuffer                  oldBacking = std::move(b.asBuffer);

    b.as       = VK_NULL_HANDLE;
    b.asBuffer = {};
    b.address  = 0;
    b.buildKey = 0;

    if (fc.deferred)
    {
        VkDevice device = m_ctx.device;
        auto*    rt     = m_ctx.rtDispatch;

        fc.deferred->enqueue(fc.frameIndex,
                             [device, rt, oldAs, backing = std::move(oldBacking)]() mutable {
                                 if (rt && device && oldAs != VK_NULL_HANDLE)
                                     rt->vkDestroyAccelerationStructureKHR(device, oldAs, nullptr);
                                 backing.destroy();
                             });
    }
    else
    {
        if (m_ctx.rtDispatch && m_ctx.device && oldAs != VK_NULL_HANDLE)
            m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, oldAs, nullptr);
        oldBacking.destroy();
    }

    m_blas.erase(it); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
}

void RtRenderer::destroyAllRtBlas() noexcept
{
    if (!m_ctx.device || !m_ctx.rtDispatch)
        return;

    for (auto& [sm, b] : m_blas)
    {
        (void)sm;

        if (b.as != VK_NULL_HANDLE)
            m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, b.as, nullptr);

        b.as = VK_NULL_HANDLE;
        b.asBuffer.destroy();
        b.address  = 0;
        b.buildKey = 0;
    }

    m_blas.clear();
}

void RtRenderer::destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept
{
    if (frameIndex >= m_tlasFrames.size())
        return;

    RtTlasFrame& t = m_tlasFrames[frameIndex];

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

void RtRenderer::destroyAllRtTlasFrames() noexcept
{
    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    for (uint32_t i = 0; i < fi; ++i)
        destroyRtTlasFrame(i, true);
}

void RtRenderer::writeRtTlasDescriptor(RtViewportState& s, uint32_t frameIndex) noexcept
{
    if (frameIndex >= m_tlasFrames.size())
        return;

    RtTlasFrame& tf = m_tlasFrames[frameIndex];
    if (tf.as == VK_NULL_HANDLE)
        return;

    // set=2, binding=2 = TLAS
    s.sets[frameIndex].writeAccelerationStructure(m_ctx.device, 2, tf.as);
}

void RtRenderer::clearRtTlasDescriptor(RtViewportState& /*s*/, uint32_t /*frameIndex*/) noexcept
{
    // Intentionally a no-op unless you enable nullDescriptor and want to write null AS.
}

//==================================================================
// Frame hooks
//==================================================================

void RtRenderer::renderPrePass(Viewport*                 vp,
                               Scene*                    scene,
                               const RenderFrameContext& fc,
                               VkDescriptorSet           set0FrameGlobals,
                               VkDescriptorSet           set1Materials)
{
    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return;

    if (!fc.cmd || !vp || !scene)
        return;

    if (!m_rtPipeline.valid() || !m_rtSbt.buffer())
        return;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (fc.frameIndex >= fi)
        return;

    if (set0FrameGlobals == VK_NULL_HANDLE || set1Materials == VK_NULL_HANDLE)
        return;

    recordTraceRays(vp, scene, fc, set0FrameGlobals, set1Materials);
}

void RtRenderer::present(VkCommandBuffer cmd, Viewport* vp, const RenderFrameContext& fc)
{
    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return;

    if (!m_rtPresent.valid())
        return;

    if (!cmd || !vp)
        return;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (fc.frameIndex >= fi)
        return;

    const uint32_t frameIdx = fc.frameIndex;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());
    if (w == 0 || h == 0)
        return;

    RtViewportState& rtv = ensureViewportState(vp, frameIdx);

    if (!ensureRtOutputImages(rtv, fc, w, h))
        return;

    vkutil::setViewportAndScissor(cmd, w, h);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rtPresent.pipeline());

    VkDescriptorSet rtSet2 = rtv.sets[frameIdx].set();
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_rtPresent.layout(),
                            0,
                            1,
                            &rtSet2,
                            0,
                            nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void RtRenderer::idle(Scene* scene)
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

        if (m_linkedMeshes.contains(mesh))
            continue;

        mesh->topology_counter()->addParent(m_tlasChangeCounter);
        mesh->deform_counter()->addParent(m_tlasChangeCounter);

        m_linkedMeshes.insert(mesh);
    }

    if (m_tlasChangeMonitor.changed())
    {
        for (RtTlasFrame& tf : m_tlasFrames)
            tf.buildKey = 0;
    }
}

//==================================================================
// RT dispatch
//==================================================================

void RtRenderer::recordTraceRays(Viewport*                 vp,
                                 Scene*                    scene,
                                 const RenderFrameContext& fc,
                                 VkDescriptorSet           set0FrameGlobals,
                                 VkDescriptorSet           set1Materials)
{
    const uint32_t frameIdx = fc.frameIndex;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());
    if (w == 0 || h == 0)
        return;

    RtViewportState& rtv = ensureViewportState(vp, frameIdx);

    if (!ensureRtOutputImages(rtv, fc, w, h))
        return;

    RtImagePerFrame& out = rtv.images[frameIdx];
    if (!out.image || !out.view)
        return;

    if (rtv.sets[frameIdx].set() == VK_NULL_HANDLE)
        return;

    // Clear RT output to viewport background (safe even if no TLAS)
    {
        const VkClearColorValue clear = vkutil::toVkClearColor(vp->clearColor());

        VkImageSubresourceRange range{};
        range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel   = 0;
        range.levelCount     = 1;
        range.baseArrayLayer = 0;
        range.layerCount     = 1;

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

    // Build/destroy BLAS for visible meshes
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

    // Ensure scene TLAS
    if (!ensureSceneTlas(vp, scene, fc))
        return;

    if (frameIdx >= m_tlasFrames.size() || m_tlasFrames[frameIdx].as == VK_NULL_HANDLE)
    {
        // leave cleared output and bail
        return;
    }

    // Bind TLAS into set=2
    writeRtTlasDescriptor(rtv, frameIdx);

    // Upload per-instance shader data + write binding 3
    {
        std::vector<RtInstanceData> instData;
        instData.reserve(scene->sceneMeshes().size());

        for (SceneMesh* sm : scene->sceneMeshes())
        {
            if (!sm || !sm->visible())
                continue;

            auto it = m_blas.find(sm);
            if (it == m_blas.end())
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

            RtInstanceData d{};
            d.posAdr   = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadePosBuffer);
            d.idxAdr   = vkutil::bufferDeviceAddress(m_ctx.device, geo.shaderIndexBuffer);
            d.nrmAdr   = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeNrmBuffer);
            d.uvAdr    = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeUvBuffer);
            d.matIdAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeMatIdBuffer);
            d.triCount = geo.shaderTriCount;

            if (d.posAdr == 0 || d.idxAdr == 0 || d.nrmAdr == 0 || d.uvAdr == 0 || d.matIdAdr == 0 || d.triCount == 0)
                continue;

            instData.push_back(d);
        }

        if (!instData.empty())
        {
            const VkDeviceSize bytes = VkDeviceSize(instData.size() * sizeof(RtInstanceData));
            rtv.instanceDataBuffers[frameIdx].upload(instData.data(), bytes);

            rtv.sets[frameIdx].writeStorageBuffer(m_ctx.device,
                                                  3,
                                                  rtv.instanceDataBuffers[frameIdx].buffer(),
                                                  bytes,
                                                  0);
        }
        else
        {
            // Keep descriptor valid, but zero-range (some drivers dislike this).
            // Use at least 1 element as a safe fallback.
            rtv.sets[frameIdx].writeStorageBuffer(m_ctx.device,
                                                  3,
                                                  rtv.instanceDataBuffers[frameIdx].buffer(),
                                                  sizeof(RtInstanceData),
                                                  0);
        }
    }

    // Transition for raygen writes
    vkutil::imageBarrier(fc.cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    // Bind RT pipeline + descriptor sets (set0,set1,set2)
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline.pipeline());

    VkDescriptorSet sets[3] = {set0FrameGlobals, set1Materials, rtv.sets[frameIdx].set()};

    vkCmdBindDescriptorSets(fc.cmd,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_rtPipeline.layout(),
                            0,
                            3,
                            sets,
                            0,
                            nullptr);

    VkStridedDeviceAddressRegionKHR rgen{};
    VkStridedDeviceAddressRegionKHR miss{};
    VkStridedDeviceAddressRegionKHR hit{};
    VkStridedDeviceAddressRegionKHR call{};
    m_rtSbt.regions(m_ctx, rgen, miss, hit, call);

    m_ctx.rtDispatch->vkCmdTraceRaysKHR(fc.cmd, &rgen, &miss, &hit, &call, w, h, 1);

    // Transition back for present sampling
    vkutil::imageBarrier(fc.cmd,
                         out.image,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

//==================================================================
// RT build helpers
//==================================================================

bool RtRenderer::ensureMeshBlas(Viewport* vp, SceneMesh* sm, const render::geom::RtMeshGeometry& geo, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !vp || !sm || !fc.cmd)
        return false;

    if (!geo.valid() || geo.buildIndexCount == 0 || geo.buildPosCount == 0)
        return false;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (fc.frameIndex >= fi)
        return false;

    uint64_t key = 0;
    key ^= uint64_t(geo.buildPosCount);
    key ^= (uint64_t(geo.buildIndexCount) << 32);

    if (sm->sysMesh())
    {
        uint64_t topo   = sm->sysMesh()->topology_counter() ? sm->sysMesh()->topology_counter()->value() : 0ull;
        uint64_t deform = sm->sysMesh()->deform_counter() ? sm->sysMesh()->deform_counter()->value() : 0ull;
        key ^= (topo + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
        key ^= (deform + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
    }

    RtBlas& b = m_blas[sm];

    if (b.as != VK_NULL_HANDLE && b.buildKey == key)
        return true;

    // Tear down existing BLAS
    if (b.as != VK_NULL_HANDLE || b.asBuffer.valid())
    {
        VkAccelerationStructureKHR oldAs      = b.as;
        GpuBuffer                  oldBacking = std::move(b.asBuffer);

        b.as       = VK_NULL_HANDLE;
        b.asBuffer = {};

        if (fc.deferred)
        {
            VkDevice device = m_ctx.device;
            auto*    rt     = m_ctx.rtDispatch;

            fc.deferred->enqueue(fc.frameIndex,
                                 [device, rt, oldAs, backing = std::move(oldBacking)]() mutable {
                                     if (rt && device && oldAs != VK_NULL_HANDLE)
                                         rt->vkDestroyAccelerationStructureKHR(device, oldAs, nullptr);
                                     backing.destroy();
                                 });
        }
        else
        {
            if (m_ctx.rtDispatch && m_ctx.device && oldAs != VK_NULL_HANDLE)
                m_ctx.rtDispatch->vkDestroyAccelerationStructureKHR(m_ctx.device, oldAs, nullptr);
            oldBacking.destroy();
        }
    }

    b.address  = 0; // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    b.buildKey = 0;

    const VkDeviceAddress vAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildPosBuffer);
    const VkDeviceAddress iAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildIndexBuffer);
    if (vAdr == 0 || iAdr == 0)
        return false;

    VkAccelerationStructureGeometryKHR asGeom{};
    asGeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeom.flags        = 0; // allow any-hit

    VkAccelerationStructureGeometryTrianglesDataKHR tri{};
    tri.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = vAdr;
    tri.vertexStride             = sizeof(glm::vec3);
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

    b.asBuffer.create(m_ctx.device,
                      m_ctx.physicalDevice,
                      sizeInfo.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

    RtViewportState& rts = ensureViewportState(vp, fc.frameIndex);
    if (!ensureRtScratch(rts, fc, sizeInfo.buildScratchSize))
        return false;

    GpuBuffer& scratch = rts.scratchBuffers[fc.frameIndex];

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

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount                                      = primCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRanges[] = {&range};

    m_ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(fc.cmd, 1, &buildInfo, pRanges);

    vkutil::barrierAsBuildToTrace(fc.cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = b.as;

    b.address  = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    b.buildKey = key;

    return (b.address != 0);
}

bool RtRenderer::ensureSceneTlas(Viewport* vp, Scene* scene, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !scene || !fc.cmd || !vp)
        return false;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (fc.frameIndex >= fi)
        return false;

    RtTlasFrame& t = m_tlasFrames[fc.frameIndex];

    // Build key: TLAS counter (optional) + all visible BLAS buildKeys
    uint64_t key = 0;
    if (m_tlasChangeCounter)
        key ^= m_tlasChangeCounter->value();

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        auto it = m_blas.find(sm);
        if (it == m_blas.end())
            continue;

        const RtBlas& b = it->second;
        if (b.as == VK_NULL_HANDLE || b.address == 0)
            continue;

        key ^= (b.buildKey + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
    }

    if (t.as != VK_NULL_HANDLE && t.buildKey == key)
        return true;

    // Instances (identity transforms for now)
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(scene->sceneMeshes().size());

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        auto it = m_blas.find(sm);
        if (it == m_blas.end())
            continue;

        const RtBlas& b = it->second;
        if (b.as == VK_NULL_HANDLE || b.address == 0)
            continue;

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
    }

    if (instances.empty())
    {
        destroyRtTlasFrame(fc.frameIndex, false);
        t.buildKey = key;
        return true;
    }

    const VkDeviceSize instanceBytes = VkDeviceSize(instances.size() * sizeof(VkAccelerationStructureInstanceKHR));

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

    t.instanceStaging.upload(instances.data(), instanceBytes);

    VkBufferCopy cpy{};
    cpy.size = instanceBytes;
    vkCmdCopyBuffer(fc.cmd, t.instanceStaging.buffer(), t.instanceBuffer.buffer(), 1, &cpy);

    vkutil::barrierTransferToAsBuildRead(fc.cmd);

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

    const bool needNewStorage =
        (!t.buffer.valid() || t.buffer.size() < sizeInfo.accelerationStructureSize || t.as == VK_NULL_HANDLE);

    if (needNewStorage)
    {
        if (t.as != VK_NULL_HANDLE || t.buffer.valid())
        {
            VkAccelerationStructureKHR oldAs      = std::exchange(t.as, VK_NULL_HANDLE);
            GpuBuffer                  oldBacking = std::exchange(t.buffer, {});

            if (fc.deferred)
            {
                VkDevice device = m_ctx.device;
                auto*    rt     = m_ctx.rtDispatch;

                fc.deferred->enqueue(fc.frameIndex,
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
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

    RtViewportState& rts = ensureViewportState(vp, fc.frameIndex);
    if (!ensureRtScratch(rts, fc, sizeInfo.buildScratchSize))
        return false;

    GpuBuffer& scratch = rts.scratchBuffers[fc.frameIndex];

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

    vkutil::barrierAsBuildToTrace(fc.cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = t.as;

    t.address  = m_ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(m_ctx.device, &addrInfo);
    t.buildKey = key;

    return (t.address != 0);
}
