#pragma once

#include <span>
#include <vulkan/vulkan.h>

class DescriptorSet
{
public:
    DescriptorSet() = default;

    // No RAII here for simplicity; pool owns the lifetime.
    bool allocate(VkDevice              device,
                  VkDescriptorPool      pool,
                  VkDescriptorSetLayout layout);

    [[nodiscard]] VkDescriptorSet set() const noexcept;

    // Convenience: write a uniform buffer at given binding.
    void writeUniformBuffer(VkDevice     device,
                            uint32_t     binding,
                            VkBuffer     buffer,
                            VkDeviceSize range,
                            VkDeviceSize offset = 0);

    // Convenience: write a storage buffer for SSBOs.
    void writeStorageBuffer(VkDevice     device,
                            uint32_t     binding,
                            VkBuffer     buffer,
                            VkDeviceSize range,
                            VkDeviceSize offset = 0);

    // Convenience: write a storage image.
    void writeStorageImage(VkDevice      device,
                           uint32_t      binding,
                           VkImageView   view,
                           VkImageLayout layout);

    // Convenience: write a combined image sampler.
    void writeCombinedImageSampler(VkDevice      device,
                                   uint32_t      binding,
                                   VkSampler     sampler,
                                   VkImageView   view,
                                   VkImageLayout layout);

    // Convenience: write an array of combined image samplers (descriptor arrays).
    void writeCombinedImageSamplerArray(VkDevice                               device,
                                        uint32_t                               binding,
                                        std::span<const VkDescriptorImageInfo> infos,
                                        uint32_t                               dstArrayElement = 0);

    // Convenience: write an acceleration structure descriptor.
    void writeAccelerationStructure(VkDevice                   device,
                                    uint32_t                   binding,
                                    VkAccelerationStructureKHR as);

private:
    VkDescriptorSet m_set{VK_NULL_HANDLE};
};
