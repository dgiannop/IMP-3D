// RtRenderer.cpp

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

// =========================================================
// RtViewportState
// =========================================================

void RtRenderer::RtViewportState::destroyDeviceResources(uint32_t framesInFlight) noexcept
{
    const uint32_t fi = std::min(framesInFlight, vkcfg::kMaxFramesInFlight);

    for (uint32_t i = 0; i < fi; ++i)
    {
        instanceDataBuffers[i].destroy();
        instanceDataBuffers[i] = {};

        scratchBuffers[i].destroy();
        scratchBuffers[i] = {};
        scratchSizes[i]   = 0;

        rtSets[i]      = {};
        presentSets[i] = {};

        presentDescriptorReady[i] = false;
    }

    // Single AOV images — destroy once
    radianceImage.destroy();
    normalImage.destroy();
    depthImage.destroy();
    albedoImage.destroy();

    cachedW = 0;
    cachedH = 0;
}

// =========================================================
// initDevice  (unchanged)
// =========================================================

bool RtRenderer::initDevice(const VulkanContext&  ctx,
                            VkDescriptorSetLayout set0Layout,
                            VkDescriptorSetLayout set1Layout)
{
    m_ctx = ctx;

    if (!m_ctx.device)
        return false;

    if (!rtReady(m_ctx) || !m_ctx.rtDispatch)
        return true;

    m_framesInFlight = std::max(1u, std::min(ctx.framesInFlight, vkcfg::kMaxFramesInFlight));

    DescriptorBindingInfo bindings[7]{};

    bindings[0].binding = 0;
    bindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    bindings[0].count   = 1;

    bindings[1].binding = 1;
    bindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].count   = 1;

    bindings[2].binding = 2;
    bindings[2].type    = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[2].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[2].count   = 1;

    bindings[3].binding = 3;
    bindings[3].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].stages  = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    bindings[3].count   = 1;

    bindings[4].binding = 4;
    bindings[4].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[4].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    bindings[4].count   = 1;

    bindings[5].binding = 5;
    bindings[5].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[5].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    bindings[5].count   = 1;

    bindings[6].binding = 6;
    bindings[6].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].stages  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
    bindings[6].count   = 1;

    m_rtSetLayout.destroy();
    if (!m_rtSetLayout.create(m_ctx.device, std::span{bindings, 7}))
    {
        std::cerr << "RtRenderer: Failed to create RT DescriptorSetLayout.\n";
        return false;
    }

    constexpr uint32_t kMaxViewports = 8;
    const uint32_t     setCount      = m_framesInFlight * kMaxViewports * 2u;

    std::array<VkDescriptorPoolSize, 4> poolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount * 4u},
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

    VkDescriptorSetLayout setLayouts[3] = {set0Layout, set1Layout, m_rtSetLayout.layout()};

    m_rtPipeline.destroy();
    if (!m_rtPipeline.createScenePipeline(m_ctx, setLayouts, 3))
    {
        std::cerr << "RtRenderer: Failed to create RT scene pipeline.\n";
        return false;
    }

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

    if (!m_denoiser.initDevice(m_ctx, m_rtFormat))
    {
        std::cerr << "RtRenderer: Failed to initialize RT denoiser.\n";
        return false;
    }

    return true;
}

// =========================================================
// shutdown
// =========================================================

void RtRenderer::shutdown() noexcept
{
    for (auto& [vp, st] : m_viewports)
    {
        (void)vp;
        st.destroyDeviceResources(m_framesInFlight);
    }
    m_viewports.clear();

    destroyAllRtTlasFrames();
    destroyAllRtBlas();

    m_rtPresent.destroy(m_ctx.device);
    m_denoiser.shutdown();

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

// =========================================================
// initSwapchain / destroySwapchainResources
// =========================================================

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

// =========================================================
// ensureViewportState
// =========================================================

RtRenderer::RtViewportState& RtRenderer::ensureViewportState(Viewport* vp, uint32_t frameIndex)
{
    RtViewportState& st = m_viewports[vp];

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (frameIndex >= fi)
        return st;

    bool needRtWrite = false;

    if (st.rtSets[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!st.rtSets[frameIndex].allocate(m_ctx.device, m_rtPool.pool(), m_rtSetLayout.layout()))
        {
            std::cerr << "RtRenderer: Failed to allocate RT set for viewport frame " << frameIndex << ".\n";
            return st;
        }
        needRtWrite = true;
    }

    if (st.presentSets[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!st.presentSets[frameIndex].allocate(m_ctx.device, m_rtPool.pool(), m_rtSetLayout.layout()))
        {
            std::cerr << "RtRenderer: Failed to allocate present set for viewport frame " << frameIndex << ".\n";
            return st;
        }
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
        needRtWrite = true;
    }

    if (needRtWrite && st.instanceDataBuffers[frameIndex].valid())
    {
        st.rtSets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                 3,
                                                 st.instanceDataBuffers[frameIndex].buffer(),
                                                 st.instanceDataBuffers[frameIndex].size(),
                                                 0);
    }

    return st;
}

// =========================================================
// ensureRtOutputImages
// =========================================================

bool RtRenderer::ensureRtOutputImages(RtViewportState&          s,
                                      const RenderFrameContext& fc,
                                      uint32_t                  w,
                                      uint32_t                  h)
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.physicalDevice)
        return false;

    if (w == 0 || h == 0)
        return false;

    constexpr VkImageUsageFlags kRtUsage =
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Single images — no per-frame index needed
    if (!s.radianceImage.ensure(m_ctx.device, m_ctx.physicalDevice, w, h, m_rtFormat, kRtUsage, fc))
        return false;
    if (!s.normalImage.ensure(m_ctx.device, m_ctx.physicalDevice, w, h, m_rtNormalFormat, kRtUsage, fc))
        return false;
    if (!s.depthImage.ensure(m_ctx.device, m_ctx.physicalDevice, w, h, m_rtDepthFormat, kRtUsage, fc))
        return false;
    if (!s.albedoImage.ensure(m_ctx.device, m_ctx.physicalDevice, w, h, m_rtAlbedoFormat, kRtUsage, fc))
        return false;

    s.cachedW = w;
    s.cachedH = h;

    return true;
}

// =========================================================
// writeRtImageDescriptors
// =========================================================

void RtRenderer::writeRtImageDescriptors(RtViewportState& s, uint32_t frameIndex) noexcept
{
    if (!m_ctx.device)
        return;

    if (s.rtSets[frameIndex].set() == VK_NULL_HANDLE)
        return;

    if (s.radianceImage.view())
        s.rtSets[frameIndex].writeStorageImage(m_ctx.device, 0, s.radianceImage.view(), VK_IMAGE_LAYOUT_GENERAL);
    if (s.normalImage.view())
        s.rtSets[frameIndex].writeStorageImage(m_ctx.device, 4, s.normalImage.view(), VK_IMAGE_LAYOUT_GENERAL);
    if (s.depthImage.view())
        s.rtSets[frameIndex].writeStorageImage(m_ctx.device, 5, s.depthImage.view(), VK_IMAGE_LAYOUT_GENERAL);
    if (s.albedoImage.view())
        s.rtSets[frameIndex].writeStorageImage(m_ctx.device, 6, s.albedoImage.view(), VK_IMAGE_LAYOUT_GENERAL);
}

// =========================================================
// writeRtPresentImageDescriptor
// =========================================================

void RtRenderer::writeRtPresentImageDescriptor(RtViewportState& s,
                                               uint32_t         frameIndex,
                                               VkImageView      view) noexcept
{
    if (!m_ctx.device || view == VK_NULL_HANDLE)
        return;

    if (s.presentSets[frameIndex].set() == VK_NULL_HANDLE)
        return;

    s.presentSets[frameIndex].writeCombinedImageSampler(m_ctx.device,
                                                        1,
                                                        m_rtSampler,
                                                        view,
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// =========================================================
// ensureRtScratch
// =========================================================

bool RtRenderer::ensureRtScratch(RtViewportState&          s,
                                 const RenderFrameContext& fc,
                                 VkDeviceSize              bytes) noexcept
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

// =========================================================
// recordTraceRays
// =========================================================

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

    writeRtImageDescriptors(rtv, frameIdx);

    VulkanImage& radiance = rtv.radianceImage;
    VulkanImage& normal   = rtv.normalImage;
    VulkanImage& depth    = rtv.depthImage;
    VulkanImage& albedo   = rtv.albedoImage;

    if (!radiance.valid() || !normal.valid() || !depth.valid() || !albedo.valid())
        return;

    if (rtv.rtSets[frameIdx].set() == VK_NULL_HANDLE)
        return;

    const VkImageSubresourceRange fullRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkClearColorValue       clearBlack{};

    for (VulkanImage* img : {&radiance, &normal, &depth, &albedo})
    {
        img->transitionToGeneral(fc.cmd);
        vkCmdClearColorImage(fc.cmd, img->image(), VK_IMAGE_LAYOUT_GENERAL, &clearBlack, 1, &fullRange);
    }

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

    if (!ensureSceneTlas(vp, scene, fc))
        return;

    if (frameIdx >= m_tlasFrames.size() || m_tlasFrames[frameIdx].as == VK_NULL_HANDLE)
        return;

    writeRtTlasDescriptor(rtv, frameIdx);

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
            if (primCount == 0 || geo.shaderTriCount != primCount)
                continue;

            RtInstanceData d{};
            d.posAdr   = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadePosBuffer);
            d.idxAdr   = vkutil::bufferDeviceAddress(m_ctx.device, geo.shaderIndexBuffer);
            d.nrmAdr   = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeNrmBuffer);
            d.uvAdr    = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeUvBuffer);
            d.matIdAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.shadeMatIdBuffer);
            d.triCount = geo.shaderTriCount;

            if (d.posAdr == 0 || d.idxAdr == 0 || d.nrmAdr == 0 ||
                d.uvAdr == 0 || d.matIdAdr == 0 || d.triCount == 0)
                continue;

            instData.push_back(d);
        }

        const VkDeviceSize bytes = instData.empty()
                                       ? sizeof(RtInstanceData)
                                       : VkDeviceSize(instData.size() * sizeof(RtInstanceData));

        if (!instData.empty())
            rtv.instanceDataBuffers[frameIdx].upload(instData.data(), bytes);

        rtv.rtSets[frameIdx].writeStorageBuffer(m_ctx.device,
                                                3,
                                                rtv.instanceDataBuffers[frameIdx].buffer(),
                                                bytes,
                                                0);
    }

    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline.pipeline());

    VkDescriptorSet sets[3] = {set0FrameGlobals, set1Materials, rtv.rtSets[frameIdx].set()};
    vkCmdBindDescriptorSets(fc.cmd,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_rtPipeline.layout(),
                            0,
                            3,
                            sets,
                            0,
                            nullptr);

    VkStridedDeviceAddressRegionKHR rgen{}, miss{}, hit{}, call{};
    m_rtSbt.regions(m_ctx, rgen, miss, hit, call);
    m_ctx.rtDispatch->vkCmdTraceRaysKHR(fc.cmd, &rgen, &miss, &hit, &call, w, h, 1);

    for (VulkanImage* img : {&radiance, &normal, &depth, &albedo})
        img->transitionToShaderRead(fc.cmd);

    if (m_denoiser.dispatch(vp, fc, radiance.view(), normal.view(), depth.view(), albedo.view(), w, h))
    {
        const VkImageView denoisedView = m_denoiser.outputView(vp, frameIdx);
        writeRtPresentImageDescriptor(rtv, frameIdx, denoisedView != VK_NULL_HANDLE ? denoisedView : radiance.view());
    }
    else
    {
        writeRtPresentImageDescriptor(rtv, frameIdx, radiance.view());
    }

    rtv.presentDescriptorReady[frameIdx] = true;
}

// =========================================================
// renderPrePass
// =========================================================

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

// =========================================================
// present
// =========================================================

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

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());
    if (w == 0 || h == 0)
        return;

    const uint32_t frameIdx = fc.frameIndex;

    RtViewportState& rtv = ensureViewportState(vp, frameIdx);

    if (!rtv.presentDescriptorReady[frameIdx])
        return;

    if (!rtv.radianceImage.valid())
        return;

    vkutil::setViewportAndScissor(cmd, w, h);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_rtPresent.pipeline());

    VkDescriptorSet rtSet2 = rtv.presentSets[frameIdx].set();
    if (rtSet2 == VK_NULL_HANDLE)
        return;

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

// =========================================================
// idle
// =========================================================

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

    m_denoiser.idle();
}

// =========================================================
// writeRtTlasDescriptor / clearRtTlasDescriptor
// =========================================================

void RtRenderer::writeRtTlasDescriptor(RtViewportState& s, uint32_t frameIndex) noexcept
{
    if (frameIndex >= m_tlasFrames.size())
        return;

    RtTlasFrame& tf = m_tlasFrames[frameIndex];
    if (tf.as == VK_NULL_HANDLE)
        return;

    if (s.rtSets[frameIndex].set() != VK_NULL_HANDLE)
        s.rtSets[frameIndex].writeAccelerationStructure(m_ctx.device, 2, tf.as);
}

void RtRenderer::clearRtTlasDescriptor(RtViewportState& /*s*/, uint32_t /*frameIndex*/) noexcept
{
}

// =========================================================
// BLAS / TLAS
// =========================================================

void RtRenderer::destroyRtBlasFor(SceneMesh* sm, const RenderFrameContext& fc) noexcept
{
    if (!rtReady(m_ctx) || !m_ctx.device || !m_ctx.rtDispatch || !sm)
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

    m_blas.erase(it);
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

bool RtRenderer::ensureMeshBlas(Viewport*                           vp,
                                SceneMesh*                          sm,
                                const render::geom::RtMeshGeometry& geo,
                                const RenderFrameContext&           fc) noexcept
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

    b.address  = 0;
    b.buildKey = 0;

    const VkDeviceAddress vAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildPosBuffer);
    const VkDeviceAddress iAdr = vkutil::bufferDeviceAddress(m_ctx.device, geo.buildIndexBuffer);
    if (vAdr == 0 || iAdr == 0)
        return false;

    VkAccelerationStructureGeometryKHR asGeom{};
    asGeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeom.flags        = 0;

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

        t.address = 0;

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
