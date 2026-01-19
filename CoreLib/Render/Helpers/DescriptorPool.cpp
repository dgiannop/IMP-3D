#include "DescriptorPool.hpp"

DescriptorPool::~DescriptorPool()
{
    destroy();
}

bool DescriptorPool::create(VkDevice                              device,
                            std::span<const VkDescriptorPoolSize> poolSizes,
                            uint32_t                              maxSets)
{
    destroy();

    m_device = device;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    ci.pPoolSizes    = poolSizes.data();
    ci.maxSets       = maxSets;
    ci.flags         = 0; // or VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT if you want

    if (vkCreateDescriptorPool(m_device, &ci, nullptr, &m_pool) != VK_SUCCESS)
    {
        m_pool   = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void DescriptorPool::destroy()
{
    if (m_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

[[nodiscard]] VkDescriptorPool DescriptorPool::pool() const noexcept
{
    return m_pool;
}
