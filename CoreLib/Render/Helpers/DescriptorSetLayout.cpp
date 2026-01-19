#include "DescriptorSetLayout.hpp"

#include <vector>

DescriptorSetLayout::~DescriptorSetLayout()
{
    destroy();
}

bool DescriptorSetLayout::create(VkDevice device, std::span<const DescriptorBindingInfo> bindings)
{
    destroy();

    m_device = device;

    // Convert our lightweight DescriptorBindingInfo → Vulkan binding structs
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(bindings.size());

    for (const auto& b : bindings)
    {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding            = b.binding;
        lb.descriptorType     = b.type;
        lb.descriptorCount    = b.count;
        lb.stageFlags         = b.stages;
        lb.pImmutableSamplers = nullptr;
        vkBindings.push_back(lb);
    }

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(vkBindings.size());
    ci.pBindings    = vkBindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_layout) != VK_SUCCESS)
    {
        m_layout = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void DescriptorSetLayout::destroy()
{
    if (m_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}
