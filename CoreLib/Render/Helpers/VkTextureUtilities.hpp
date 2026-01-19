// ============================================================================
// Device-Local Image Helpers (textures)
// ============================================================================

#include <vulkan/vulkan_core.h>

#include "VulkanContext.hpp"

namespace vkutil
{
    struct GpuImage
    {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;

        int32_t  width     = 0;
        int32_t  height    = 0;
        uint32_t mipLevels = 1;
        VkFormat format    = VK_FORMAT_UNDEFINED;

        [[nodiscard]] bool valid() const noexcept
        {
            return image != VK_NULL_HANDLE && memory != VK_NULL_HANDLE;
        }
    };

    /// Create a device-local 2D image.
    GpuImage createDeviceLocalImage2D(const VulkanContext& ctx,
                                      int32_t              width,
                                      int32_t              height,
                                      uint32_t             mipLevels,
                                      VkFormat             format,
                                      VkImageUsageFlags    usage);

    /// Transition image layout with a transient command buffer.
    void transitionImageLayout(const VulkanContext& ctx,
                               VkImage              image,
                               VkFormat             format,
                               VkImageLayout        oldLayout,
                               VkImageLayout        newLayout,
                               uint32_t             mipLevels);

    /// Copy a buffer into the base mip of an image.
    void copyBufferToImage(const VulkanContext& ctx,
                           VkBuffer             buffer,
                           VkImage              image,
                           int32_t              width,
                           int32_t              height);

    /// Generate mipmaps for a 2D color image (linear blit).
    void generateMipmaps(const VulkanContext& ctx,
                         VkImage              image,
                         int32_t              width,
                         int32_t              height,
                         uint32_t             mipLevels);

} // namespace vkutil
