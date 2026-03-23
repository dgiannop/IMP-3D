#include "RtDenoiser.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>

#include "ShaderStage.hpp"
#include "Viewport.hpp"
#include "VkUtilities.hpp"

namespace
{
    constexpr uint32_t kGroupSizeX = 8;
    constexpr uint32_t kGroupSizeY = 8;

    constexpr VkPipelineStageFlags kComputeStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
} // namespace

// =========================================================
// ViewportState
// =========================================================

void RtDenoiser::ViewportState::destroyDeviceResources(uint32_t framesInFlight) noexcept
{
    const uint32_t fi = std::min(framesInFlight, vkcfg::kMaxFramesInFlight);

    for (uint32_t i = 0; i < fi; ++i)
    {
        filterSetsA[i] = {};
        filterSetsB[i] = {};
        filterSetsC[i] = {};
        copySets[i]    = {};
    }

    // Single images — destroy once
    pingImage.destroy();
    pongImage.destroy();
    outImage.destroy();

    cachedW = 0;
    cachedH = 0;
}

// =========================================================
// initDevice  (unchanged)
// =========================================================

bool RtDenoiser::initDevice(const VulkanContext& ctx, VkFormat colorFormat)
{
    m_ctx         = ctx;
    m_colorFormat = colorFormat;

    if (!m_ctx.device)
        return false;

    m_framesInFlight = std::max(1u, std::min(ctx.framesInFlight, vkcfg::kMaxFramesInFlight));

    {
        DescriptorBindingInfo bindings[5] = {};

        bindings[0].binding = 0;
        bindings[0].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[0].count   = 1;

        bindings[1].binding = 1;
        bindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].count   = 1;

        bindings[2].binding = 2;
        bindings[2].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[2].count   = 1;

        bindings[3].binding = 3;
        bindings[3].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[3].count   = 1;

        bindings[4].binding = 4;
        bindings[4].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[4].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[4].count   = 1;

        m_filterSetLayout.destroy();
        if (!m_filterSetLayout.create(m_ctx.device, std::span{bindings, 5}))
        {
            std::cerr << "RtDenoiser: failed to create filter set layout.\n";
            return false;
        }
    }

    {
        DescriptorBindingInfo bindings[2] = {};

        bindings[0].binding = 0;
        bindings[0].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[0].count   = 1;

        bindings[1].binding = 1;
        bindings[1].type    = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].stages  = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].count   = 1;

        m_copySetLayout.destroy();
        if (!m_copySetLayout.create(m_ctx.device, std::span{bindings, 2}))
        {
            std::cerr << "RtDenoiser: failed to create copy set layout.\n";
            return false;
        }
    }

    constexpr uint32_t kMaxViewports  = 8;
    const uint32_t     filterSetCount = m_framesInFlight * kMaxViewports * 3u;
    const uint32_t     copySetCount   = m_framesInFlight * kMaxViewports;
    const uint32_t     totalSetCount  = filterSetCount + copySetCount;

    std::array<VkDescriptorPoolSize, 2> poolSizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, filterSetCount * 4u + copySetCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, filterSetCount + copySetCount},
    };

    m_pool.destroy();
    if (!m_pool.create(m_ctx.device, poolSizes, totalSetCount))
    {
        std::cerr << "RtDenoiser: failed to create descriptor pool.\n";
        return false;
    }

    if (m_sampler == VK_NULL_HANDLE)
    {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = VK_FILTER_LINEAR;
        sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod       = 0.0f;

        if (vkCreateSampler(m_ctx.device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
        {
            std::cerr << "RtDenoiser: failed to create sampler.\n";
            return false;
        }
    }

    if (m_filterPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_filterPipelineLayout, nullptr);
        m_filterPipelineLayout = VK_NULL_HANDLE;
    }
    {
        VkDescriptorSetLayout setLayout = m_filterSetLayout.layout();

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset     = 0;
        pcRange.size       = sizeof(FilterPushConstants);

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &setLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pcRange;

        if (vkCreatePipelineLayout(m_ctx.device, &plci, nullptr, &m_filterPipelineLayout) != VK_SUCCESS)
        {
            std::cerr << "RtDenoiser: failed to create filter pipeline layout.\n";
            return false;
        }
    }

    if (m_copyPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_copyPipelineLayout, nullptr);
        m_copyPipelineLayout = VK_NULL_HANDLE;
    }
    {
        VkDescriptorSetLayout setLayout = m_copySetLayout.layout();

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset     = 0;
        pcRange.size       = sizeof(CopyPushConstants);

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &setLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pcRange;

        if (vkCreatePipelineLayout(m_ctx.device, &plci, nullptr, &m_copyPipelineLayout) != VK_SUCCESS)
        {
            std::cerr << "RtDenoiser: failed to create copy pipeline layout.\n";
            return false;
        }
    }

    if (m_filterPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_ctx.device, m_filterPipeline, nullptr);
        m_filterPipeline = VK_NULL_HANDLE;
    }
    {
        const std::filesystem::path shaderPath =
            std::filesystem::path(SHADER_BIN_DIR) / "RtDenoiseAtrous.comp.spv";

        ShaderStage comp = ShaderStage::fromSpirvFile(m_ctx.device, shaderPath, VK_SHADER_STAGE_COMPUTE_BIT);
        if (!comp.isValid())
        {
            std::cerr << "RtDenoiser: failed to load filter shader.\n";
            return false;
        }

        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage  = comp.stageInfo();
        cpci.layout = m_filterPipelineLayout;

        if (vkCreateComputePipelines(m_ctx.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_filterPipeline) != VK_SUCCESS)
        {
            std::cerr << "RtDenoiser: failed to create filter pipeline.\n";
            return false;
        }
    }

    if (m_copyPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_ctx.device, m_copyPipeline, nullptr);
        m_copyPipeline = VK_NULL_HANDLE;
    }
    {
        const std::filesystem::path shaderPath =
            std::filesystem::path(SHADER_BIN_DIR) / "RtDenoiseCopy.comp.spv";

        ShaderStage comp = ShaderStage::fromSpirvFile(m_ctx.device, shaderPath, VK_SHADER_STAGE_COMPUTE_BIT);
        if (!comp.isValid())
        {
            std::cerr << "RtDenoiser: failed to load copy shader.\n";
            return false;
        }

        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage  = comp.stageInfo();
        cpci.layout = m_copyPipelineLayout;

        if (vkCreateComputePipelines(m_ctx.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_copyPipeline) != VK_SUCCESS)
        {
            std::cerr << "RtDenoiser: failed to create copy pipeline.\n";
            return false;
        }
    }

    return true;
}

// =========================================================
// shutdown
// =========================================================

void RtDenoiser::shutdown() noexcept
{
    for (auto& [vp, state] : m_viewports)
    {
        (void)vp;
        state.destroyDeviceResources(m_framesInFlight);
    }
    m_viewports.clear();

    if (m_filterPipeline != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipeline(m_ctx.device, m_filterPipeline, nullptr);
        m_filterPipeline = VK_NULL_HANDLE;
    }

    if (m_copyPipeline != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipeline(m_ctx.device, m_copyPipeline, nullptr);
        m_copyPipeline = VK_NULL_HANDLE;
    }

    if (m_filterPipelineLayout != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_filterPipelineLayout, nullptr);
        m_filterPipelineLayout = VK_NULL_HANDLE;
    }

    if (m_copyPipelineLayout != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_copyPipelineLayout, nullptr);
        m_copyPipelineLayout = VK_NULL_HANDLE;
    }

    if (m_sampler != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroySampler(m_ctx.device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    m_pool.destroy();
    m_filterSetLayout.destroy();
    m_copySetLayout.destroy();

    m_ctx            = {};
    m_framesInFlight = 1;
    m_colorFormat    = VK_FORMAT_UNDEFINED;
}

// =========================================================
// ensureViewportState
// =========================================================

RtDenoiser::ViewportState& RtDenoiser::ensureViewportState(Viewport* vp, uint32_t frameIndex)
{
    ViewportState& state = m_viewports[vp];

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (frameIndex >= fi)
        return state;

    if (state.filterSetsA[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!state.filterSetsA[frameIndex].allocate(m_ctx.device, m_pool.pool(), m_filterSetLayout.layout()))
            std::cerr << "RtDenoiser: failed to allocate filter set A.\n";
    }

    if (state.filterSetsB[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!state.filterSetsB[frameIndex].allocate(m_ctx.device, m_pool.pool(), m_filterSetLayout.layout()))
            std::cerr << "RtDenoiser: failed to allocate filter set B.\n";
    }

    if (state.filterSetsC[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!state.filterSetsC[frameIndex].allocate(m_ctx.device, m_pool.pool(), m_filterSetLayout.layout()))
            std::cerr << "RtDenoiser: failed to allocate filter set C.\n";
    }

    if (state.copySets[frameIndex].set() == VK_NULL_HANDLE)
    {
        if (!state.copySets[frameIndex].allocate(m_ctx.device, m_pool.pool(), m_copySetLayout.layout()))
            std::cerr << "RtDenoiser: failed to allocate copy set.\n";
    }

    return state;
}

// =========================================================
// ensureImages
// =========================================================

bool RtDenoiser::ensureImages(ViewportState&            s,
                              const RenderFrameContext& fc,
                              uint32_t                  width,
                              uint32_t                  height)
{
    if (!m_ctx.device || !m_ctx.physicalDevice)
        return false;

    if (width == 0 || height == 0)
        return false;

    constexpr VkImageUsageFlags kUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // Single images — no per-frame index needed
    if (!s.pingImage.ensure(m_ctx.device, m_ctx.physicalDevice, width, height, m_colorFormat, kUsage, fc))
        return false;
    if (!s.pongImage.ensure(m_ctx.device, m_ctx.physicalDevice, width, height, m_colorFormat, kUsage, fc))
        return false;
    if (!s.outImage.ensure(m_ctx.device, m_ctx.physicalDevice, width, height, m_colorFormat, kUsage, fc))
        return false;

    s.cachedW = width;
    s.cachedH = height;
    return true;
}

// =========================================================
// Descriptor writes  (unchanged)
// =========================================================

void RtDenoiser::writeFilterDescriptors(DescriptorSet& set,
                                        VkImageView    colorView,
                                        VkImageView    normalView,
                                        VkImageView    depthView,
                                        VkImageView    albedoView,
                                        VkImageView    outView) noexcept
{
    if (!m_ctx.device)
        return;

    set.writeCombinedImageSampler(m_ctx.device, 0, m_sampler, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.writeCombinedImageSampler(m_ctx.device, 1, m_sampler, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.writeCombinedImageSampler(m_ctx.device, 2, m_sampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.writeCombinedImageSampler(m_ctx.device, 3, m_sampler, albedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.writeStorageImage(m_ctx.device, 4, outView, VK_IMAGE_LAYOUT_GENERAL);
}

void RtDenoiser::writeCopyDescriptors(DescriptorSet& set,
                                      VkImageView    inView,
                                      VkImageView    outView) noexcept
{
    if (!m_ctx.device)
        return;

    set.writeCombinedImageSampler(m_ctx.device, 0, m_sampler, inView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.writeStorageImage(m_ctx.device, 1, outView, VK_IMAGE_LAYOUT_GENERAL);
}

// =========================================================
// dispatch
// =========================================================

bool RtDenoiser::dispatch(Viewport*                 vp,
                          const RenderFrameContext& fc,
                          VkImageView               radianceView,
                          VkImageView               normalView,
                          VkImageView               depthView,
                          VkImageView               albedoView,
                          uint32_t                  width,
                          uint32_t                  height)
{
    if (!m_ctx.device || !fc.cmd || !vp)
        return false;

    if (radianceView == VK_NULL_HANDLE || normalView == VK_NULL_HANDLE ||
        depthView == VK_NULL_HANDLE || albedoView == VK_NULL_HANDLE)
        return false;

    if (m_filterPipeline == VK_NULL_HANDLE || m_filterPipelineLayout == VK_NULL_HANDLE)
        return false;

    if (m_copyPipeline == VK_NULL_HANDLE || m_copyPipelineLayout == VK_NULL_HANDLE)
        return false;

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (fc.frameIndex >= fi)
        return false;

    ViewportState& state = ensureViewportState(vp, fc.frameIndex);

    if (!ensureImages(state, fc, width, height))
        return false;

    VulkanImage& ping = state.pingImage;
    VulkanImage& pong = state.pongImage;
    VulkanImage& out  = state.outImage;

    if (!ping.valid() || !pong.valid() || !out.valid())
        return false;

    const uint32_t gx = (width + kGroupSizeX - 1u) / kGroupSizeX;
    const uint32_t gy = (height + kGroupSizeY - 1u) / kGroupSizeY;

    // Pass 1: radiance -> ping
    writeFilterDescriptors(state.filterSetsA[fc.frameIndex],
                           radianceView,
                           normalView,
                           depthView,
                           albedoView,
                           ping.view());
    {
        VkDescriptorSet set = state.filterSetsA[fc.frameIndex].set();
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipeline);
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipelineLayout, 0, 1, &set, 0, nullptr);
    }
    ping.transitionToGeneral(fc.cmd, kComputeStage);
    {
        FilterPushConstants pc{};
        pc.width     = width;
        pc.height    = height;
        pc.stepWidth = 1;
        pc.colorPhi  = 14.0f;
        pc.normalPhi = 96.0f;
        pc.depthPhi  = 2.5f;
        pc.albedoPhi = 12.0f;
        vkCmdPushConstants(fc.cmd, m_filterPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(fc.cmd, gx, gy, 1);
    }
    ping.transitionToShaderRead(fc.cmd, kComputeStage, kComputeStage);

    // Pass 2: ping -> pong
    writeFilterDescriptors(state.filterSetsB[fc.frameIndex],
                           ping.view(),
                           normalView,
                           depthView,
                           albedoView,
                           pong.view());
    {
        VkDescriptorSet set = state.filterSetsB[fc.frameIndex].set();
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipeline);
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipelineLayout, 0, 1, &set, 0, nullptr);
    }
    pong.transitionToGeneral(fc.cmd, kComputeStage);
    {
        FilterPushConstants pc{};
        pc.width     = width;
        pc.height    = height;
        pc.stepWidth = 2;
        pc.colorPhi  = 12.0f;
        pc.normalPhi = 96.0f;
        pc.depthPhi  = 2.5f;
        pc.albedoPhi = 10.0f;
        vkCmdPushConstants(fc.cmd, m_filterPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(fc.cmd, gx, gy, 1);
    }
    pong.transitionToShaderRead(fc.cmd, kComputeStage, kComputeStage);

    // Pass 3: pong -> ping
    writeFilterDescriptors(state.filterSetsC[fc.frameIndex],
                           pong.view(),
                           normalView,
                           depthView,
                           albedoView,
                           ping.view());
    {
        VkDescriptorSet set = state.filterSetsC[fc.frameIndex].set();
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipeline);
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipelineLayout, 0, 1, &set, 0, nullptr);
    }
    ping.transitionToGeneral(fc.cmd, kComputeStage);
    {
        FilterPushConstants pc{};
        pc.width     = width;
        pc.height    = height;
        pc.stepWidth = 4;
        pc.colorPhi  = 10.0f;
        pc.normalPhi = 96.0f;
        pc.depthPhi  = 2.5f;
        pc.albedoPhi = 8.0f;
        vkCmdPushConstants(fc.cmd, m_filterPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(fc.cmd, gx, gy, 1);
    }
    ping.transitionToShaderRead(fc.cmd, kComputeStage, kComputeStage);

    // Pass 4: ping -> out (copy)
    writeCopyDescriptors(state.copySets[fc.frameIndex], ping.view(), out.view());
    {
        VkDescriptorSet set = state.copySets[fc.frameIndex].set();
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_copyPipeline);
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_copyPipelineLayout, 0, 1, &set, 0, nullptr);
    }
    out.transitionToGeneral(fc.cmd, kComputeStage);
    {
        CopyPushConstants pc{};
        pc.width  = width;
        pc.height = height;
        vkCmdPushConstants(fc.cmd, m_copyPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(fc.cmd, gx, gy, 1);
    }
    out.transitionToShaderRead(fc.cmd, kComputeStage, kComputeStage);

    return true;
}

// =========================================================
// outputView / outputImage
// =========================================================

VkImageView RtDenoiser::outputView(Viewport* vp, uint32_t frameIndex) const noexcept
{
    (void)frameIndex; // no longer per-frame

    auto it = m_viewports.find(vp);
    if (it == m_viewports.end())
        return VK_NULL_HANDLE;

    return it->second.outImage.view();
}

VkImage RtDenoiser::outputImage(Viewport* vp, uint32_t frameIndex) const noexcept
{
    (void)frameIndex; // no longer per-frame

    auto it = m_viewports.find(vp);
    if (it == m_viewports.end())
        return VK_NULL_HANDLE;

    return it->second.outImage.image();
}
