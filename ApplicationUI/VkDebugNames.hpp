//============================================================
// VkDebugNames.hpp  (UI/VulkanBackend layer)
//============================================================
// Debug-only Vulkan object naming (compiled out in Release).
// No Qt includes required.

#pragma once

#include <cstdint>
#include <cstdio>
#include <vulkan/vulkan.h>

#if !defined(NDEBUG)
#define VKUTIL_DEBUG_NAMES 1
#else
#define VKUTIL_DEBUG_NAMES 0
#endif

namespace vkutil
{
#if VKUTIL_DEBUG_NAMES

    // Cache per-process. Fine for a single device (your current case).
    // If we ever support multiple VkDevice objects, we can extend this to a small map.
    inline PFN_vkSetDebugUtilsObjectNameEXT g_setName = nullptr;

    // Call once after we have VkDevice and a way to get device proc addrs.
    inline void init(PFN_vkGetDeviceProcAddr getDeviceProcAddr, VkDevice device) noexcept
    {
        g_setName = nullptr;

        if (!getDeviceProcAddr || !device)
            return;

        g_setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            getDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    }

    inline void shutdown() noexcept
    {
        g_setName = nullptr;
    }

    inline void setObjectName(VkDevice     device,
                              VkObjectType type,
                              uint64_t     objectHandle,
                              const char*  baseName,
                              int32_t      index = -1) noexcept
    {
        if (!device || !g_setName || !objectHandle || !baseName)
            return;

        char nameBuf[128] = {};
        if (index >= 0)
            std::snprintf(nameBuf, sizeof(nameBuf), "%s [%d]", baseName, index);
        else
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", baseName);

        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType   = type;
        info.objectHandle = objectHandle;
        info.pObjectName  = nameBuf;

        g_setName(device, &info);
    }

    // ------------------------------------------------------------
    // Convenience overloads (just device + object + name)
    // ------------------------------------------------------------

    inline void name(VkDevice device, VkBuffer obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_BUFFER, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkImage obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_IMAGE, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkImageView obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkPipeline obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_PIPELINE, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkPipelineLayout obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkDescriptorSet obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkDescriptorSetLayout obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkCommandBuffer obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_COMMAND_BUFFER, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkCommandPool obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_COMMAND_POOL, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkSemaphore obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkFence obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_FENCE, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkFramebuffer obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_FRAMEBUFFER, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkRenderPass obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_RENDER_PASS, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkSampler obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_SAMPLER, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkSwapchainKHR obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_SWAPCHAIN_KHR, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkAccelerationStructureKHR obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, uint64_t(obj), baseName, index);
    }

    inline void name(VkDevice device, VkDeviceMemory obj, const char* baseName, int32_t index = -1) noexcept
    {
        setObjectName(device, VK_OBJECT_TYPE_DEVICE_MEMORY, uint64_t(obj), baseName, index);
    }

#else

    // Release: everything is compiled out
    inline void init(PFN_vkGetDeviceProcAddr, VkDevice) noexcept
    {
    }
    inline void shutdown() noexcept
    {
    }

    inline void setObjectName(VkDevice, VkObjectType, uint64_t, const char*, int32_t = -1) noexcept
    {
    }

    inline void name(VkDevice, VkBuffer, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkImage, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkImageView, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkPipeline, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkPipelineLayout, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkDescriptorSet, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkDescriptorSetLayout, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkCommandBuffer, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkCommandPool, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkSemaphore, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkFence, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkFramebuffer, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkRenderPass, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkSampler, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkSwapchainKHR, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkAccelerationStructureKHR, const char*, int32_t = -1) noexcept
    {
    }
    inline void name(VkDevice, VkDeviceMemory, const char*, int32_t = -1) noexcept
    {
    }

#endif
} // namespace vkutil
