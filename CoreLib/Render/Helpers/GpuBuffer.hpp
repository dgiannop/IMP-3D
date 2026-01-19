#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

/**
 * Lightweight RAII wrapper around a Vulkan buffer + its device memory.
 *
 * - No implicit allocation in default ctor.
 * - Explicit create() / destroy().
 * - Move-only (no accidental copies).
 * - Works for vertex / index / uniform / storage / staging buffers.
 * - Optional persistent mapping for HOST_VISIBLE buffers (good for UBOs).
 *
 * Note:
 *   upload() will transparently grow/recreate the buffer if offset+size
 *   exceeds current capacity, but old contents are NOT preserved.
 *   This is intended for transient data (UBOs, staging, etc.).
 */
class GpuBuffer
{
public:
    GpuBuffer() = default;
    ~GpuBuffer();

    GpuBuffer(const GpuBuffer&)            = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    void create(VkDevice              device,
                VkPhysicalDevice      physicalDevice,
                VkDeviceSize          size,
                VkBufferUsageFlags    usage,
                VkMemoryPropertyFlags memoryFlags,
                bool                  persistentMap = false,
                bool                  deviceAddress = false);

    void destroy();

    /// Upload into a HOST_VISIBLE buffer.
    ///
    /// If offset+size exceeds the current capacity, the buffer is destroyed
    /// and recreated with a new size (>= offset+size) using the same device,
    /// physical device, usage flags, memory flags, and mapping mode.
    ///
    /// Previous contents are NOT preserved when this happens.
    void upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    [[nodiscard]] bool valid() const
    {
        return m_buffer != VK_NULL_HANDLE;
    }

    [[nodiscard]] VkBuffer buffer() const
    {
        return m_buffer;
    }

    [[nodiscard]] VkDeviceMemory memory() const
    {
        return m_memory;
    }

    [[nodiscard]] VkDeviceSize size() const
    {
        return m_size;
    }

private:
    uint32_t findMemoryType(uint32_t bits, VkPhysicalDevice phys, VkMemoryPropertyFlags flags);
    void     moveFrom(GpuBuffer&& other);

private:
    VkDevice              m_device        = VK_NULL_HANDLE;
    VkPhysicalDevice      m_physDevice    = VK_NULL_HANDLE;
    VkBuffer              m_buffer        = VK_NULL_HANDLE;
    VkDeviceMemory        m_memory        = VK_NULL_HANDLE;
    void*                 m_mapped        = nullptr;
    VkDeviceSize          m_size          = 0;
    VkBufferUsageFlags    m_usage         = 0;
    VkMemoryPropertyFlags m_memFlags      = 0;
    bool                  m_persistent    = false;
    bool                  m_deviceAddress = false;
};
