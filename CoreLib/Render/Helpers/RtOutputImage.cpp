#include "RtOutputImage.hpp"

#include <algorithm>
#include <iostream>

uint32_t RtOutputImage::findMemoryType(uint32_t              typeBits,
                                       VkPhysicalDevice      phys,
                                       VkMemoryPropertyFlags flags) noexcept
{
    VkPhysicalDeviceMemoryProperties mp = {};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return 0;
}

void RtOutputImage::destroyImage(const VulkanContext& ctx) noexcept
{
    if (!ctx.device)
        return;

    if (m_view)
        vkDestroyImageView(ctx.device, m_view, nullptr);
    m_view = VK_NULL_HANDLE;

    if (m_image)
        vkDestroyImage(ctx.device, m_image, nullptr);
    m_image = VK_NULL_HANDLE;

    if (m_mem)
        vkFreeMemory(ctx.device, m_mem, nullptr);
    m_mem = VK_NULL_HANDLE;

    if (m_sampler)
        vkDestroySampler(ctx.device, m_sampler, nullptr);
    m_sampler = VK_NULL_HANDLE;

    m_width  = 0;
    m_height = 0;
}

void RtOutputImage::destroy(const VulkanContext& ctx) noexcept
{
    destroyImage(ctx);

    if (ctx.device && m_pool)
        vkDestroyDescriptorPool(ctx.device, m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
    m_set  = VK_NULL_HANDLE;

    if (ctx.device && m_setLayout)
        vkDestroyDescriptorSetLayout(ctx.device, m_setLayout, nullptr);
    m_setLayout = VK_NULL_HANDLE;
}

bool RtOutputImage::createDescriptors(const VulkanContext& ctx)
{
    // Descriptor Set Layout:
    //  binding 0: storage image   (raygen)
    //  binding 1: combined sampler (fullscreen present)
    VkDescriptorSetLayoutBinding b0 = {};
    b0.binding                      = 0;
    b0.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b0.descriptorCount              = 1;
    b0.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    b0.pImmutableSamplers           = nullptr;

    VkDescriptorSetLayoutBinding b1 = {};
    b1.binding                      = 1;
    b1.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b1.descriptorCount              = 1;
    b1.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;
    b1.pImmutableSamplers           = nullptr;

    VkDescriptorSetLayoutBinding bindings[2] = {b0, b1};

    VkDescriptorSetLayoutCreateInfo slci = {};
    slci.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slci.bindingCount                    = 2;
    slci.pBindings                       = bindings;

    if (vkCreateDescriptorSetLayout(ctx.device, &slci, nullptr, &m_setLayout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps[2] = {};
    ps[0].type                 = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ps[0].descriptorCount      = 1;
    ps[1].type                 = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount      = 1;

    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets                    = 1;
    dpci.poolSizeCount              = 2;
    dpci.pPoolSizes                 = ps;

    if (vkCreateDescriptorPool(ctx.device, &dpci, nullptr, &m_pool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo ai = {};
    ai.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool              = m_pool;
    ai.descriptorSetCount          = 1;
    ai.pSetLayouts                 = &m_setLayout;

    if (vkAllocateDescriptorSets(ctx.device, &ai, &m_set) != VK_SUCCESS)
        return false;

    return true;
}

bool RtOutputImage::createImage(const VulkanContext& ctx, uint32_t w, uint32_t h)
{
    // Output format: keep it simple.
    // VK_FORMAT_R8G8B8A8_UNORM works almost everywhere.
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo ici = {};
    ici.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType         = VK_IMAGE_TYPE_2D;
    ici.format            = fmt;
    ici.extent            = {w, h, 1};
    ici.mipLevels         = 1;
    ici.arrayLayers       = 1;
    ici.samples           = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling            = VK_IMAGE_TILING_OPTIMAL;
    ici.usage             = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ctx.device, &ici, nullptr, &m_image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr = {};
    vkGetImageMemoryRequirements(ctx.device, m_image, &mr);

    VkMemoryAllocateInfo mai = {};
    mai.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize       = mr.size;
    mai.memoryTypeIndex      = findMemoryType(mr.memoryTypeBits, ctx.physicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (mai.memoryTypeIndex == 0 && (mr.memoryTypeBits & 1u) == 0)
        return false;

    if (vkAllocateMemory(ctx.device, &mai, nullptr, &m_mem) != VK_SUCCESS)
        return false;

    if (vkBindImageMemory(ctx.device, m_image, m_mem, 0) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo vci           = {};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = m_image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = fmt;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(ctx.device, &vci, nullptr, &m_view) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo sci     = {};
    sci.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter               = VK_FILTER_LINEAR;
    sci.minFilter               = VK_FILTER_LINEAR;
    sci.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod                  = 0.0f;
    sci.maxLod                  = 0.0f;
    sci.maxAnisotropy           = 1.0f;
    sci.anisotropyEnable        = VK_FALSE;
    sci.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(ctx.device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
        return false;

    m_width  = w;
    m_height = h;

    return true;
}

bool RtOutputImage::createOrResize(const VulkanContext& ctx, uint32_t width, uint32_t height)
{
    if (!ctx.device || !ctx.physicalDevice)
        return false;

    if (!m_setLayout)
    {
        if (!createDescriptors(ctx))
            return false;
    }

    if (width == 0 || height == 0)
        return false;

    if (m_image && width == m_width && height == m_height)
        return true;

    // Destroy image resources only; keep descriptor objects.
    destroyImage(ctx);

    if (!createImage(ctx, width, height))
        return false;

    // Update descriptors (both bindings reference same view; use GENERAL to keep it simple)
    VkDescriptorImageInfo storageInfo = {};
    storageInfo.imageView             = m_view;
    storageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo sampleInfo = {};
    sampleInfo.sampler               = m_sampler;
    sampleInfo.imageView             = m_view;
    sampleInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2] = {};

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
    writes[1].pImageInfo      = &sampleInfo;

    vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);

    return true;
}
