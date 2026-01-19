#pragma once

#include <vulkan/vulkan.h>

#include "VulkanContext.hpp"

class RtOutputImage final
{
public:
    RtOutputImage()  = default;
    ~RtOutputImage() = default;

    RtOutputImage(const RtOutputImage&)            = delete;
    RtOutputImage& operator=(const RtOutputImage&) = delete;

    void destroy(const VulkanContext& ctx) noexcept;

    bool createOrResize(const VulkanContext& ctx, uint32_t width, uint32_t height);

    VkDescriptorSetLayout setLayout() const noexcept
    {
        return m_setLayout;
    }
    VkDescriptorSet set() const noexcept
    {
        return m_set;
    }

    VkImage image() const noexcept
    {
        return m_image;
    }
    VkImageView view() const noexcept
    {
        return m_view;
    }
    VkSampler sampler() const noexcept
    {
        return m_sampler;
    }

    uint32_t width() const noexcept
    {
        return m_width;
    }
    uint32_t height() const noexcept
    {
        return m_height;
    }

private:
    bool     createDescriptors(const VulkanContext& ctx);
    bool     createImage(const VulkanContext& ctx, uint32_t w, uint32_t h);
    void     destroyImage(const VulkanContext& ctx) noexcept;
    uint32_t findMemoryType(uint32_t typeBits, VkPhysicalDevice phys, VkMemoryPropertyFlags flags) noexcept;

private:
    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    VkImage        m_image   = VK_NULL_HANDLE;
    VkDeviceMemory m_mem     = VK_NULL_HANDLE;
    VkImageView    m_view    = VK_NULL_HANDLE;
    VkSampler      m_sampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_pool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_set       = VK_NULL_HANDLE;
};
