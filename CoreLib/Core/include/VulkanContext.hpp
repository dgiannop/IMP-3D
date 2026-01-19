#pragma once

#include <cstdint>
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

inline bool rtReady(const VulkanContext& ctx) noexcept
{
    return ctx.supportsRayTracing && ctx.rtDispatch != nullptr;
}
