#include "VulkanImage.hpp"

#include <cassert>
#include <iostream>

#include "VkUtilities.hpp"

// =========================================================
// Move semantics
// =========================================================

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
    : m_device(other.m_device),
      m_image(other.m_image),
      m_memory(other.m_memory),
      m_view(other.m_view),
      m_width(other.m_width),
      m_height(other.m_height),
      m_format(other.m_format),
      m_aspectMask(other.m_aspectMask),
      m_layout(other.m_layout),
      m_needsInit(other.m_needsInit)
{
    other.m_device     = VK_NULL_HANDLE;
    other.m_image      = VK_NULL_HANDLE;
    other.m_memory     = VK_NULL_HANDLE;
    other.m_view       = VK_NULL_HANDLE;
    other.m_width      = 0;
    other.m_height     = 0;
    other.m_format     = VK_FORMAT_UNDEFINED;
    other.m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    other.m_layout     = VK_IMAGE_LAYOUT_UNDEFINED;
    other.m_needsInit  = true;
}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept
{
    if (this != &other)
    {
        destroy();

        m_device     = other.m_device;
        m_image      = other.m_image;
        m_memory     = other.m_memory;
        m_view       = other.m_view;
        m_width      = other.m_width;
        m_height     = other.m_height;
        m_format     = other.m_format;
        m_aspectMask = other.m_aspectMask;
        m_layout     = other.m_layout;
        m_needsInit  = other.m_needsInit;

        other.m_device     = VK_NULL_HANDLE;
        other.m_image      = VK_NULL_HANDLE;
        other.m_memory     = VK_NULL_HANDLE;
        other.m_view       = VK_NULL_HANDLE;
        other.m_width      = 0;
        other.m_height     = 0;
        other.m_format     = VK_FORMAT_UNDEFINED;
        other.m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        other.m_layout     = VK_IMAGE_LAYOUT_UNDEFINED;
        other.m_needsInit  = true;
    }
    return *this;
}

// =========================================================
// aspectMaskForFormat
// =========================================================

VkImageAspectFlags VulkanImage::aspectMaskForFormat(VkFormat format) noexcept
{
    switch (format)
    {
        // Depth-only
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

            // Depth + stencil
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

            // Colour (everything else)
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// =========================================================
// create
// =========================================================

bool VulkanImage::create(VkDevice          device,
                         VkPhysicalDevice  physicalDevice,
                         uint32_t          width,
                         uint32_t          height,
                         VkFormat          format,
                         VkImageUsageFlags usage)
{
    assert(device != VK_NULL_HANDLE);
    assert(physicalDevice != VK_NULL_HANDLE);
    assert(width > 0);
    assert(height > 0);
    assert(format != VK_FORMAT_UNDEFINED);
    assert(m_image == VK_NULL_HANDLE && "Call ensure() to resize a live image");

    // ---- VkImage ----

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = format;
    ici.extent        = VkExtent3D{width, height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &m_image) != VK_SUCCESS)
    {
        std::cerr << "VulkanImage: vkCreateImage failed "
                  << "(format=" << format
                  << ", " << width << "x" << height << ").\n";
        return false;
    }

    // ---- VkDeviceMemory ----

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, m_image, &req);

    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((req.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            typeIndex = i;
            break;
        }
    }

    if (typeIndex == UINT32_MAX)
    {
        std::cerr << "VulkanImage: No device-local memory type available.\n";
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = typeIndex;

    if (vkAllocateMemory(device, &mai, nullptr, &m_memory) != VK_SUCCESS)
    {
        std::cerr << "VulkanImage: vkAllocateMemory failed.\n";
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS)
    {
        std::cerr << "VulkanImage: vkBindImageMemory failed.\n";
        vkFreeMemory(device, m_memory, nullptr);
        vkDestroyImage(device, m_image, nullptr);
        m_image  = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
        return false;
    }

    // ---- VkImageView ----

    // Derive the aspect mask from the format so depth/stencil images
    // get the correct view without requiring the caller to specify it.
    const VkImageAspectFlags aspect = aspectMaskForFormat(format);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image                           = m_image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = format;
    vci.subresourceRange.aspectMask     = aspect;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &vci, nullptr, &m_view) != VK_SUCCESS)
    {
        std::cerr << "VulkanImage: vkCreateImageView failed.\n";
        vkFreeMemory(device, m_memory, nullptr);
        vkDestroyImage(device, m_image, nullptr);
        m_image  = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
        return false;
    }

    m_device     = device;
    m_width      = width;
    m_height     = height;
    m_format     = format;
    m_aspectMask = aspect;
    m_layout     = VK_IMAGE_LAYOUT_UNDEFINED;
    m_needsInit  = true;

    return true;
}

// =========================================================
// ensure
// =========================================================

bool VulkanImage::ensure(VkDevice          device,
                         VkPhysicalDevice  physicalDevice,
                         uint32_t          width,
                         uint32_t          height,
                         VkFormat          format,
                         VkImageUsageFlags usage,
                         DeferredDeletion* deferred,
                         uint32_t          frameIndex)
{
    // Fast path — already the right size and format.
    if (m_image != VK_NULL_HANDLE &&
        m_view != VK_NULL_HANDLE &&
        m_width == width &&
        m_height == height &&
        m_format == format)
    {
        return true;
    }

    // Release existing resources before (re-)creating.
    if (m_image || m_view || m_memory)
    {
        // Capture handles by value and zero the members first so the object
        // is in a clean empty state before any deferred enqueue or direct
        // destroy executes (guards against re-entrancy / exception safety).
        const VkDevice       oldDevice = m_device;
        const VkImage        oldImage  = m_image;
        const VkDeviceMemory oldMemory = m_memory;
        const VkImageView    oldView   = m_view;

        m_device     = VK_NULL_HANDLE;
        m_image      = VK_NULL_HANDLE;
        m_memory     = VK_NULL_HANDLE;
        m_view       = VK_NULL_HANDLE;
        m_width      = 0;
        m_height     = 0;
        m_format     = VK_FORMAT_UNDEFINED;
        m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        m_layout     = VK_IMAGE_LAYOUT_UNDEFINED;
        m_needsInit  = true;

        auto doDestroy = [oldDevice, oldView, oldImage, oldMemory]() noexcept {
            if (oldView)
                vkDestroyImageView(oldDevice, oldView, nullptr);
            if (oldImage)
                vkDestroyImage(oldDevice, oldImage, nullptr);
            if (oldMemory)
                vkFreeMemory(oldDevice, oldMemory, nullptr);
        };

        if (deferred)
            deferred->enqueue(frameIndex, std::move(doDestroy));
        else
            doDestroy();
    }

    return create(device, physicalDevice, width, height, format, usage);
}

bool VulkanImage::ensure(VkDevice                  device,
                         VkPhysicalDevice          physicalDevice,
                         uint32_t                  width,
                         uint32_t                  height,
                         VkFormat                  format,
                         VkImageUsageFlags         usage,
                         const RenderFrameContext& fc)
{
    return ensure(device, physicalDevice, width, height, format, usage, fc.deferred, fc.frameIndex);
}

// =========================================================
// destroy
// =========================================================

void VulkanImage::destroy() noexcept
{
    if (!m_device)
        return;

    if (m_view)
    {
        vkDestroyImageView(m_device, m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image)
    {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory)
    {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }

    m_device     = VK_NULL_HANDLE;
    m_width      = 0;
    m_height     = 0;
    m_format     = VK_FORMAT_UNDEFINED;
    m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    m_layout     = VK_IMAGE_LAYOUT_UNDEFINED;
    m_needsInit  = true;
}

// =========================================================
// Layout transitions
// =========================================================

void VulkanImage::transitionToGeneral(VkCommandBuffer      cmd,
                                      VkPipelineStageFlags dstStage) noexcept
{
    assert(cmd != VK_NULL_HANDLE);
    assert(m_image != VK_NULL_HANDLE);

    VkAccessFlags        srcAccess = 0;
    VkPipelineStageFlags srcStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    switch (m_layout)
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            srcAccess = 0;
            srcStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            srcAccess = VK_ACCESS_SHADER_READ_BIT;
            srcStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            srcStage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;

        default:
            srcAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            srcStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
    }

    // dstAccessMask must only contain access types supported by dstStage.
    // TRANSFER_WRITE is valid for transfer stages but not compute — so only
    // include it when the destination stage actually covers transfer.
    const bool dstIncludesTransfer = (dstStage & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0;

    const VkAccessFlags dstAccess = VK_ACCESS_SHADER_WRITE_BIT |
                                    (dstIncludesTransfer ? VK_ACCESS_TRANSFER_WRITE_BIT : 0u);

    vkutil::imageBarrier(cmd,
                         m_image,
                         m_layout,
                         VK_IMAGE_LAYOUT_GENERAL,
                         srcAccess,
                         dstAccess,
                         srcStage,
                         dstStage,
                         m_aspectMask,
                         0,
                         1,
                         0,
                         1);

    m_layout    = VK_IMAGE_LAYOUT_GENERAL;
    m_needsInit = false;
}

void VulkanImage::transitionToShaderRead(VkCommandBuffer      cmd,
                                         VkPipelineStageFlags srcStage,
                                         VkPipelineStageFlags dstStage) noexcept
{
    assert(cmd != VK_NULL_HANDLE);
    assert(m_image != VK_NULL_HANDLE);
    assert(m_layout == VK_IMAGE_LAYOUT_GENERAL &&
           "VulkanImage::transitionToShaderRead requires GENERAL as source layout");

    vkutil::imageBarrier(cmd,
                         m_image,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT,
                         srcStage,
                         dstStage,
                         m_aspectMask,
                         0, // baseMip,
                         1, // mipCount
                         0, // baseLayer
                         1  // layerCount
    );

    m_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
