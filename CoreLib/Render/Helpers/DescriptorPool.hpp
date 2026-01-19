#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan.h>

class DescriptorPool
{
public:
    DescriptorPool() = default;
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&)            = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    DescriptorPool(DescriptorPool&&)                 = delete;
    DescriptorPool& operator=(DescriptorPool&&)      = delete;

    bool create(VkDevice                              device,
                std::span<const VkDescriptorPoolSize> poolSizes,
                uint32_t                              maxSets);

    void destroy();

    [[nodiscard]] VkDescriptorPool pool() const noexcept;

private:
    VkDevice         m_device{VK_NULL_HANDLE};
    VkDescriptorPool m_pool{VK_NULL_HANDLE};
};
