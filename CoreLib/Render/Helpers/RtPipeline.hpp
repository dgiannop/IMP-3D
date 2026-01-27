//============================================================
// RtPipeline.hpp
//============================================================
#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

struct VulkanContext;

namespace vkrt
{
    /**
     * @brief Minimal RT pipeline wrapper (scene pipeline only).
     *
     * Creates:
     *  - VkPipelineLayout (descriptor set layouts provided by caller)
     *  - VkPipeline (raygen + miss + closest hit)
     *
     * Used by SBT build + vkCmdTraceRaysKHR.
     */
    class RtPipeline final
    {
    public:
        RtPipeline()  = default;
        ~RtPipeline() = default;

        RtPipeline(const RtPipeline&)            = delete;
        RtPipeline& operator=(const RtPipeline&) = delete;

        RtPipeline(RtPipeline&& other) noexcept;
        RtPipeline& operator=(RtPipeline&& other) noexcept;

        void destroy() noexcept;

        // Scene pipeline (supports multiple descriptor sets)
        bool createScenePipeline(const VulkanContext&         ctx,
                                 const VkDescriptorSetLayout* setLayouts,
                                 uint32_t                     setLayoutCount);

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

        // Group counts for this pipeline (used by SBT creation).
        static constexpr uint32_t kRaygenCount   = 1;
        static constexpr uint32_t kMissCount     = 1;
        static constexpr uint32_t kHitCount      = 1;
        static constexpr uint32_t kCallableCount = 0;
        static constexpr uint32_t kGroupCount =
            kRaygenCount + kMissCount + kHitCount + kCallableCount;

    private:
        void moveFrom(RtPipeline&& other) noexcept;

    private:
        VkDevice         m_device   = VK_NULL_HANDLE;
        VkPipelineLayout m_layout   = VK_NULL_HANDLE;
        VkPipeline       m_pipeline = VK_NULL_HANDLE;
    };

} // namespace vkrt
