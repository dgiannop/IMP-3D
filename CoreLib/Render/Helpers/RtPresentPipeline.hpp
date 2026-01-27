#pragma once

#include <vulkan/vulkan.h>

namespace vkrt
{
    /**
     * @brief Minimal graphics pipeline for presenting the RT result.
     *
     * Pipeline:
     *  - Fullscreen triangle
     *  - No depth
     *  - Single color attachment
     *
     * Layout:
     *  - Single descriptor set (RT set: storage image + sampler + TLAS, etc.).
     *
     * NOTE:
     *  - Only Vulkan *types* appear in the header.
     *  - All vk* function calls live in the .cpp file.
     */
    class RtPresentPipeline final
    {
    public:
        RtPresentPipeline()  = default;
        ~RtPresentPipeline() = default;

        RtPresentPipeline(const RtPresentPipeline&)            = delete;
        RtPresentPipeline& operator=(const RtPresentPipeline&) = delete;

        RtPresentPipeline(RtPresentPipeline&& other) noexcept;
        RtPresentPipeline& operator=(RtPresentPipeline&& other) noexcept;

        /**
         * @brief Destroy pipeline & layout using the given device.
         */
        void destroy(VkDevice device) noexcept;

        /**
         * @brief Create the RT present pipeline + layout.
         *
         * @param device       Vulkan device.
         * @param renderPass   Render pass used for the viewport swapchain.
         * @param sampleCount  MSAA sample count for the swapchain.
         * @param setLayout    Descriptor set layout for the RT set (Set 2).
         */
        bool create(VkDevice              device,
                    VkRenderPass          renderPass,
                    VkSampleCountFlagBits sampleCount,
                    VkDescriptorSetLayout setLayout);

        [[nodiscard]] VkPipeline pipeline() const noexcept
        {
            return m_pipeline;
        }

        [[nodiscard]] VkPipelineLayout layout() const noexcept
        {
            return m_layout;
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return m_pipeline != VK_NULL_HANDLE && m_layout != VK_NULL_HANDLE;
        }

    private:
        void moveFrom(RtPresentPipeline&& other) noexcept;

    private:
        VkPipelineLayout m_layout   = VK_NULL_HANDLE;
        VkPipeline       m_pipeline = VK_NULL_HANDLE;
    };

} // namespace vkrt
