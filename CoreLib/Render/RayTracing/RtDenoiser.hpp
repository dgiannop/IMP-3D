#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vulkan/vulkan.h>

#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "VulkanContext.hpp"
#include "VulkanImage.hpp"

class Viewport;
struct RenderFrameContext;

class RtDenoiser final
{
public:
    RtDenoiser() noexcept  = default;
    ~RtDenoiser() noexcept = default;

    RtDenoiser(const RtDenoiser&)            = delete;
    RtDenoiser& operator=(const RtDenoiser&) = delete;

public:
    [[nodiscard]] bool initDevice(const VulkanContext& ctx, VkFormat colorFormat);
    void               shutdown() noexcept;

    void idle() noexcept {}

    [[nodiscard]] bool dispatch(Viewport*                 vp,
                                const RenderFrameContext& fc,
                                VkImageView               radianceView,
                                VkImageView               normalView,
                                VkImageView               depthView,
                                VkImageView               albedoView,
                                uint32_t                  width,
                                uint32_t                  height);

    [[nodiscard]] VkImageView outputView(Viewport* vp, uint32_t frameIndex) const noexcept;
    [[nodiscard]] VkImage     outputImage(Viewport* vp, uint32_t frameIndex) const noexcept;

private:
    struct ViewportState
    {
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> filterSetsA = {};
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> filterSetsB = {};
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> filterSetsC = {};
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> copySets    = {};

        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> pingImages = {};
        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> pongImages = {};
        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> outImages  = {};

        uint32_t cachedW = 0;
        uint32_t cachedH = 0;

        void destroyDeviceResources(uint32_t framesInFlight) noexcept;
    };

    struct FilterPushConstants
    {
        uint32_t width     = 0;
        uint32_t height    = 0;
        uint32_t stepWidth = 1;
        float    colorPhi  = 10.0f;
        float    normalPhi = 48.0f;
        float    depthPhi  = 2.0f;
        float    albedoPhi = 8.0f;
    };

    struct CopyPushConstants
    {
        uint32_t width  = 0;
        uint32_t height = 0;
    };

private:
    ViewportState& ensureViewportState(Viewport* vp, uint32_t frameIndex);

    [[nodiscard]] bool ensureImages(ViewportState&            s,
                                    const RenderFrameContext& fc,
                                    uint32_t                  width,
                                    uint32_t                  height);

    void writeFilterDescriptors(DescriptorSet& set,
                                VkImageView    colorView,
                                VkImageView    normalView,
                                VkImageView    depthView,
                                VkImageView    albedoView,
                                VkImageView    outView) noexcept;

    void writeCopyDescriptors(DescriptorSet& set,
                              VkImageView    inView,
                              VkImageView    outView) noexcept;

private:
    VulkanContext m_ctx            = {};
    uint32_t      m_framesInFlight = 1;
    VkFormat      m_colorFormat    = VK_FORMAT_UNDEFINED;
    VkSampler     m_sampler        = VK_NULL_HANDLE;

    DescriptorSetLayout m_filterSetLayout = {};
    DescriptorSetLayout m_copySetLayout   = {};
    DescriptorPool      m_pool            = {};

    VkPipelineLayout m_filterPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_copyPipelineLayout   = VK_NULL_HANDLE;

    VkPipeline m_filterPipeline = VK_NULL_HANDLE;
    VkPipeline m_copyPipeline   = VK_NULL_HANDLE;

    std::unordered_map<Viewport*, ViewportState> m_viewports = {};
};
