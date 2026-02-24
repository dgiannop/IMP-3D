//==============================================================
// RtOutputImage.cpp
//==============================================================
#include "RtOutputImage.hpp"

#include <cassert>

namespace
{
    // IMPORTANT:
    // This must match your RT output image format used elsewhere (Renderer::m_rtFormat)
    // and what your shaders/present pipeline expect.
    //
    // If your project already has a single source of truth for the RT format,
    // change this to use it (or pass it in via VulkanContext).
    constexpr VkFormat kRtOutputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
} // namespace

void RtOutputImage::destroy(const VulkanContext& ctx) noexcept
{
    if (!ctx.device)
        return;

    VkDevice device = ctx.device;

    destroyImage(ctx);

    if (m_set != VK_NULL_HANDLE)
        m_set = VK_NULL_HANDLE; // pool-owned

    if (m_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }

    if (m_setLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }

    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    m_width  = 0;
    m_height = 0;
}

bool RtOutputImage::createOrResize(const VulkanContext& ctx, uint32_t width, uint32_t height)
{
    if (!ctx.device || !ctx.physicalDevice)
        return false;

    if (width == 0 || height == 0)
        return false;

    // Lazy-create descriptors once.
    if (m_setLayout == VK_NULL_HANDLE || m_pool == VK_NULL_HANDLE || m_set == VK_NULL_HANDLE || m_sampler == VK_NULL_HANDLE)
    {
        if (!createDescriptors(ctx))
            return false;
    }

    // Already correct size.
    if (m_image != VK_NULL_HANDLE && m_view != VK_NULL_HANDLE && m_width == width && m_height == height)
        return true;

    // Recreate image + view.
    destroyImage(ctx);
    if (!createImage(ctx, width, height))
        return false;

    // Update descriptors.
    // binding 0: storage image (raygen writes)  -> GENERAL
    // binding 1: sampled image (present reads)  -> SHADER_READ_ONLY_OPTIMAL
    {
        VkDescriptorImageInfo storageInfo{};
        storageInfo.imageView   = m_view;
        storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo sampledInfo{};
        sampledInfo.sampler     = m_sampler;
        sampledInfo.imageView   = m_view;
        sampledInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2]{};

        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_set;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo      = &storageInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_set;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo      = &sampledInfo;

        vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);
    }

    return true;
}

bool RtOutputImage::createDescriptors(const VulkanContext& ctx)
{
    if (!ctx.device)
        return false;

    VkDevice device = ctx.device;

    // ------------------------------------------------------------
    // Sampler (for present sampling)
    // ------------------------------------------------------------
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

        if (vkCreateSampler(device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
            return false;
    }

    // ------------------------------------------------------------
    // Set layout:
    //   binding 0 = storage image (raygen writes)
    //   binding 1 = combined image sampler (present reads)
    // ------------------------------------------------------------
    if (m_setLayout == VK_NULL_HANDLE)
    {
        VkDescriptorSetLayoutBinding b0{};
        b0.binding         = 0;
        b0.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b0.descriptorCount = 1;
        b0.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding b1{};
        b1.binding         = 1;
        b1.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b1.descriptorCount = 1;
        b1.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding bindings[2] = {b0, b1};

        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = 2;
        lci.pBindings    = bindings;

        if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_setLayout) != VK_SUCCESS)
            return false;
    }

    // ------------------------------------------------------------
    // Pool (one set)
    // ------------------------------------------------------------
    if (m_pool == VK_NULL_HANDLE)
    {
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[0].descriptorCount = 1;
        sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.maxSets       = 1;
        pci.poolSizeCount = 2;
        pci.pPoolSizes    = sizes;

        if (vkCreateDescriptorPool(device, &pci, nullptr, &m_pool) != VK_SUCCESS)
            return false;
    }

    // ------------------------------------------------------------
    // Allocate set
    // ------------------------------------------------------------
    if (m_set == VK_NULL_HANDLE)
    {
        VkDescriptorSetAllocateInfo asi{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        asi.descriptorPool     = m_pool;
        asi.descriptorSetCount = 1;
        asi.pSetLayouts        = &m_setLayout;

        if (vkAllocateDescriptorSets(device, &asi, &m_set) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool RtOutputImage::createImage(const VulkanContext& ctx, uint32_t w, uint32_t h)
{
    if (!ctx.device || !ctx.physicalDevice)
        return false;

    VkDevice device = ctx.device;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = kRtOutputFormat;
    ici.extent        = VkExtent3D{w, h, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &m_image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, m_image, &req);

    const uint32_t typeIndex = findMemoryType(req.memoryTypeBits, ctx.physicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (typeIndex == UINT32_MAX)
    {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = typeIndex;

    if (vkAllocateMemory(device, &mai, nullptr, &m_mem) != VK_SUCCESS)
    {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(device, m_image, m_mem, 0) != VK_SUCCESS)
    {
        vkFreeMemory(device, m_mem, nullptr);
        vkDestroyImage(device, m_image, nullptr);
        m_mem   = VK_NULL_HANDLE;
        m_image = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image                           = m_image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = kRtOutputFormat;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &vci, nullptr, &m_view) != VK_SUCCESS)
    {
        vkFreeMemory(device, m_mem, nullptr);
        vkDestroyImage(device, m_image, nullptr);
        m_mem   = VK_NULL_HANDLE;
        m_image = VK_NULL_HANDLE;
        return false;
    }

    m_width  = w;
    m_height = h;
    return true;
}

void RtOutputImage::destroyImage(const VulkanContext& ctx) noexcept
{
    if (!ctx.device)
        return;

    VkDevice device = ctx.device;

    if (m_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_mem != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_mem, nullptr);
        m_mem = VK_NULL_HANDLE;
    }

    m_width  = 0;
    m_height = 0;
}

uint32_t RtOutputImage::findMemoryType(uint32_t              typeBits,
                                       VkPhysicalDevice      phys,
                                       VkMemoryPropertyFlags flags) noexcept
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && ((memProps.memoryTypes[i].propertyFlags & flags) == flags))
            return i;
    }
    return UINT32_MAX;
}
