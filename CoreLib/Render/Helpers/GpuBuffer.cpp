#include "GpuBuffer.hpp"

#include <cstring>  // memcpy
#include <iostream> // optional: for error logging
#include <utility>

// --------------------------------------------------------
// Helpers
// --------------------------------------------------------

uint32_t GpuBuffer::findMemoryType(uint32_t              bits,
                                   VkPhysicalDevice      phys,
                                   VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);

    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
    {
        if ((bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & flags) == flags)
        {
            return i;
        }
    }

    // In production you'd probably assert or throw here.
    return 0;
}

// --------------------------------------------------------
// Create / destroy
// --------------------------------------------------------
void GpuBuffer::create(VkDevice              device,
                       VkPhysicalDevice      physicalDevice,
                       VkDeviceSize          size,
                       VkBufferUsageFlags    usage,
                       VkMemoryPropertyFlags memoryFlags,
                       bool                  persistentMap,
                       bool                  deviceAddress)
{
    destroy();

    m_device        = device;
    m_physDevice    = physicalDevice;
    m_size          = size;
    m_usage         = usage;
    m_memFlags      = memoryFlags;
    m_persistent    = persistentMap;
    m_deviceAddress = deviceAddress;

    if (m_deviceAddress)
        m_usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = m_size;
    bi.usage       = m_usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bi, nullptr, &m_buffer) != VK_SUCCESS)
    {
        m_buffer = VK_NULL_HANDLE;
        return;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(m_device, m_buffer, &req);

    const uint32_t memType = findMemoryType(req.memoryTypeBits, m_physDevice, m_memFlags);
    if (memType == UINT32_MAX)
    {
        destroy();
        return;
    }

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = memType;

    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (m_deviceAddress)
    {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        ai.pNext        = &flagsInfo;
    }

    if (vkAllocateMemory(m_device, &ai, nullptr, &m_memory) != VK_SUCCESS)
    {
        destroy();
        return;
    }

    if (vkBindBufferMemory(m_device, m_buffer, m_memory, 0) != VK_SUCCESS)
    {
        destroy();
        return;
    }

    if (m_persistent)
    {
        vkMapMemory(m_device, m_memory, 0, m_size, 0, &m_mapped);
    }
}

void GpuBuffer::destroy()
{
    if (!m_device)
        return;

    if (m_persistent && m_mapped)
    {
        vkUnmapMemory(m_device, m_memory);
        m_mapped = nullptr;
    }

    if (m_buffer)
    {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }

    if (m_memory)
    {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }

    m_device        = VK_NULL_HANDLE;
    m_physDevice    = VK_NULL_HANDLE;
    m_size          = 0;
    m_usage         = 0;
    m_memFlags      = 0;
    m_persistent    = false;
    m_deviceAddress = false;
}

// --------------------------------------------------------
// Upload with transparent resize (HOST_VISIBLE only)
// --------------------------------------------------------

void GpuBuffer::upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    if (!data || size == 0)
        return;

    if (!m_device || !m_physDevice)
    {
        std::cerr << "GpuBuffer::upload: buffer not created yet.\n";
        return;
    }

    // Sanity check: this should only be used on HOST_VISIBLE memory.
    if ((m_memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
    {
        std::cerr << "GpuBuffer::upload: called on non-HOST_VISIBLE buffer. "
                     "Use staging + vkCmdCopyBuffer instead.\n";
        return;
    }

    const VkDeviceSize required = offset + size;

    // Grow if needed (old contents are discarded).
    if (required > m_size)
    {
        // Optional: grow with factor to reduce realloc churn:
        // VkDeviceSize newSize = std::max(required, m_size + m_size / 2);
        VkDeviceSize newSize = required;

        create(m_device, m_physDevice, newSize, m_usage, m_memFlags, m_persistent, m_deviceAddress);

        if (!valid())
        {
            std::cerr << "GpuBuffer::upload: recreate failed.\n";
            return;
        }
    }

    // Now [offset, offset+size) fits.
    if (!m_memory)
        return;

    void* ptr = m_persistent ? m_mapped : nullptr;

    if (!ptr)
    {
        VkResult res = vkMapMemory(m_device, m_memory, offset, size, 0, &ptr);
        if (res != VK_SUCCESS || !ptr)
        {
            std::cerr << "GpuBuffer::upload: vkMapMemory failed.\n";
            return;
        }
    }

    std::memcpy(static_cast<char*>(ptr) + static_cast<std::size_t>(offset),
                data,
                static_cast<std::size_t>(size));

    if (!m_persistent)
    {
        vkUnmapMemory(m_device, m_memory);
    }
}

// --------------------------------------------------------
// Move / dtor
// --------------------------------------------------------

GpuBuffer::GpuBuffer(GpuBuffer&& o) noexcept
{
    moveFrom(std::move(o));
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& o) noexcept
{
    if (this != &o)
    {
        destroy();
        moveFrom(std::move(o));
    }
    return *this;
}

void GpuBuffer::moveFrom(GpuBuffer&& o)
{
    m_device        = o.m_device;
    m_physDevice    = o.m_physDevice;
    m_buffer        = o.m_buffer;
    m_memory        = o.m_memory;
    m_mapped        = o.m_mapped;
    m_size          = o.m_size;
    m_usage         = o.m_usage;
    m_memFlags      = o.m_memFlags;
    m_persistent    = o.m_persistent;
    m_deviceAddress = o.m_deviceAddress;

    o.m_device        = VK_NULL_HANDLE;
    o.m_physDevice    = VK_NULL_HANDLE;
    o.m_buffer        = VK_NULL_HANDLE;
    o.m_memory        = VK_NULL_HANDLE;
    o.m_mapped        = nullptr;
    o.m_size          = 0;
    o.m_usage         = 0;
    o.m_memFlags      = 0;
    o.m_persistent    = false;
    o.m_deviceAddress = false;
}

GpuBuffer::~GpuBuffer()
{
    destroy();
}
