#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

/**
 * @brief Optional ray tracing dispatch table (device-level function pointers).
 *
 * If VulkanContext::supportsRayTracing == true, rtDispatch is non-null and
 * contains the required device entry points for RT.
 *
 * Lifetime: owned by the UI/backend layer that produced the VulkanContext.
 * Treat as read-only in CoreLib.
 */
struct VulkanRtDispatch
{
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;

    PFN_vkCreateAccelerationStructureKHR           vkCreateAccelerationStructureKHR           = nullptr;
    PFN_vkDestroyAccelerationStructureKHR          vkDestroyAccelerationStructureKHR          = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR    vkGetAccelerationStructureBuildSizesKHR    = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR        vkCmdBuildAccelerationStructuresKHR        = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;

    PFN_vkCreateRayTracingPipelinesKHR       vkCreateRayTracingPipelinesKHR       = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR                    vkCmdTraceRaysKHR                    = nullptr;
};

/**
 * @brief Per-frame deferred destruction queue.
 *
 * Used to delay destruction of Vulkan resources until it is safe with respect
 * to GPU work in flight.
 *
 * The queue is indexed by "frame-in-flight slot" (frameIndex), not swapchain
 * image index. Typical usage:
 *  - In beginFrame(): after waiting for the fence of slot fi, call flush(fi)
 *    to destroy resources queued the last time fi was used.
 *  - During rendering/recreation: enqueue(fi, ...) to schedule destruction for
 *    when that same slot becomes safe again.
 *
 * Notes:
 *  - Not thread-safe.
 *  - Enqueued callables may capture state. Ensure captured objects outlive the
 *    eventual flush().
 */
#include <functional> // still
// ...

struct DeferredDeletion
{
    std::vector<std::vector<std::move_only_function<void()>>> perFrame;

    void init(uint32_t framesInFlight)
    {
        perFrame.clear();
        perFrame.resize(framesInFlight);
    }

    void enqueue(uint32_t frameIndex, std::move_only_function<void()>&& fn)
    {
        if (frameIndex >= perFrame.size())
            return;
        perFrame[frameIndex].push_back(std::move(fn));
    }

    void flush(uint32_t frameIndex)
    {
        if (frameIndex >= perFrame.size())
            return;

        auto& q = perFrame[frameIndex];
        for (auto& fn : q)
            fn();
        q.clear();
    }
};

/**
 * @brief Small, per-call context passed down from UI into CoreLib render functions.
 *
 * This avoids having to extend render() / renderPrePass() signatures every time
 * we need one more per-frame/per-viewport bit of data.
 *
 * Lifetime:
 *  - cmd is valid for the duration of the current frame recording.
 *  - deferred typically points to the per-viewport swapchain deferred queue.
 */
struct RenderFrameContext
{
    VkCommandBuffer   cmd        = VK_NULL_HANDLE;
    uint32_t          frameIndex = 0;
    DeferredDeletion* deferred   = nullptr; ///< points to per-viewport deferred queue (e.g. ViewportSwapchain::deferred)
};

/**
 * @brief CoreLib Vulkan device context provided by the UI layer.
 *
 * CoreLib uses this to create and own device resources (buffers, images, pipelines, etc).
 * UI owns surfaces/swapchains/presentation and simply passes the long-lived device handles.
 */
struct VulkanContext
{
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;

    VkQueue  graphicsQueue            = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = 0;

    VkSampleCountFlagBits sampleCount    = VK_SAMPLE_COUNT_1_BIT;
    uint32_t              framesInFlight = 2;

    VkPhysicalDeviceProperties deviceProps{};

    bool supportsRayTracing = false;

    // Optional RT dispatch table (only valid if supportsRayTracing == true)
    const VulkanRtDispatch* rtDispatch = nullptr;

    // Useful RT properties (SBT sizes/alignments, recursion limit)
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR    rtProps = {};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps = {};

    // Optional opaque handle for a memory allocator (e.g. VMA) or other shared backend state.
    void* allocator = nullptr;
};

[[nodiscard]] inline bool rtReady(const VulkanContext& ctx) noexcept
{
    return ctx.supportsRayTracing && ctx.rtDispatch != nullptr;
}
