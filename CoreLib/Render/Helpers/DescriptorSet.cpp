#include "DescriptorSet.hpp"

bool DescriptorSet::allocate(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout;

    return (vkAllocateDescriptorSets(device, &ai, &m_set) == VK_SUCCESS);
}

VkDescriptorSet DescriptorSet::set() const noexcept
{
    return m_set;
}

void DescriptorSet::writeUniformBuffer(VkDevice     device,
                                       uint32_t     binding,
                                       VkBuffer     buffer,
                                       VkDeviceSize range,
                                       VkDeviceSize offset)
{
    VkDescriptorBufferInfo bi{};
    bi.buffer = buffer;
    bi.offset = offset;
    bi.range  = range;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = m_set;
    w.dstBinding      = binding;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.descriptorCount = 1;
    w.pBufferInfo     = &bi;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

void DescriptorSet::writeStorageBuffer(VkDevice     device,
                                       uint32_t     binding,
                                       VkBuffer     buffer,
                                       VkDeviceSize range,
                                       VkDeviceSize offset)
{
    VkDescriptorBufferInfo bi{};
    bi.buffer = buffer;
    bi.offset = offset;
    bi.range  = range;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = m_set;
    w.dstBinding      = binding;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.descriptorCount = 1;
    w.pBufferInfo     = &bi;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

void DescriptorSet::writeStorageImage(VkDevice      device,
                                      uint32_t      binding,
                                      VkImageView   view,
                                      VkImageLayout layout)
{
    VkDescriptorImageInfo ii{};
    ii.imageView   = view;
    ii.imageLayout = layout;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = m_set;
    w.dstBinding      = binding;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.descriptorCount = 1;
    w.pImageInfo      = &ii;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

void DescriptorSet::writeCombinedImageSampler(VkDevice      device,
                                              uint32_t      binding,
                                              VkSampler     sampler,
                                              VkImageView   view,
                                              VkImageLayout layout)
{
    VkDescriptorImageInfo ii{};
    ii.sampler     = sampler;
    ii.imageView   = view;
    ii.imageLayout = layout;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = m_set;
    w.dstBinding      = binding;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo      = &ii;

    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}
