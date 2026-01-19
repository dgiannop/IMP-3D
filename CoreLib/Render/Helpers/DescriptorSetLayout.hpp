#pragma once

#include <cstdint>
#include <span>
#include <vulkan/vulkan.h>

struct DescriptorBindingInfo
{
    uint32_t           binding = 0;
    VkDescriptorType   type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkShaderStageFlags stages  = VK_SHADER_STAGE_VERTEX_BIT;
    uint32_t           count   = 1; // array size, usually 1
};

class DescriptorSetLayout
{
public:
    DescriptorSetLayout() = default;
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&)            = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout(DescriptorSetLayout&&)                 = delete;
    DescriptorSetLayout& operator=(DescriptorSetLayout&&)      = delete;

    bool create(VkDevice device, std::span<const DescriptorBindingInfo> bindings);
    void destroy();

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept
    {
        return m_layout;
    }

private:
    VkDevice              m_device{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_layout{VK_NULL_HANDLE};
};
