#pragma once

#include <vulkan/vulkan.h>

#include "VulkanContext.hpp"

class RtPresentPipeline final
{
public:
    RtPresentPipeline()  = default;
    ~RtPresentPipeline() = default;

    RtPresentPipeline(const RtPresentPipeline&)            = delete;
    RtPresentPipeline& operator=(const RtPresentPipeline&) = delete;

    void destroy(const VulkanContext& ctx) noexcept;

    bool create(const VulkanContext&  ctx,
                VkRenderPass          renderPass,
                VkSampleCountFlagBits sampleCount,
                VkDescriptorSetLayout setLayout,
                VkShaderModule        fullscreenVs,
                VkShaderModule        presentFs);

    VkPipeline pipeline() const noexcept
    {
        return m_pipeline;
    }
    VkPipelineLayout layout() const noexcept
    {
        return m_layout;
    }

private:
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
    VkPipeline       m_pipeline = VK_NULL_HANDLE;
};
