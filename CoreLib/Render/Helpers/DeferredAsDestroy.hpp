#pragma once

#include <vector>
#include <vulkan/vulkan.h>

#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

struct VulkanContext;

// Holds AS handle + its backing buffer, destroyed later.
struct DeferredAsItem
{
    VkAccelerationStructureKHR as      = VK_NULL_HANDLE;
    GpuBuffer                  backing = {};
};

class DeferredAsDestroy
{
public:
    DeferredAsDestroy() = default;

    void init(uint32_t framesInFlight)
    {
        m_bins.clear();
        m_bins.resize(framesInFlight);
    }

    void shutdown(const VulkanContext& ctx) noexcept
    {
        // Last resort: ensure nothing in flight, then destroy everything.
        if (ctx.device)
            vkDeviceWaitIdle(ctx.device);

        flushAll(ctx);
        m_bins.clear();
    }

    void enqueue(uint32_t frameIndex, VkAccelerationStructureKHR as, GpuBuffer&& backing)
    {
        if (as == VK_NULL_HANDLE && !backing.valid())
            return;

        if (m_bins.empty())
            return;

        frameIndex = frameIndex % static_cast<uint32_t>(m_bins.size());

        DeferredAsItem item = {};
        item.as             = as;
        item.backing        = std::move(backing);

        m_bins[frameIndex].push_back(std::move(item));
    }

    // Call this ONLY when you KNOW the GPU has finished executing work for frameIndex
    // (i.e., after waiting/signaling that frame's fence).
    void flushFrame(uint32_t frameIndex, const VulkanContext& ctx) noexcept
    {
        if (m_bins.empty())
            return;

        frameIndex = frameIndex % static_cast<uint32_t>(m_bins.size());

        auto& bin = m_bins[frameIndex];
        if (bin.empty())
            return;

        destroyBin(bin, ctx);
        bin.clear();
        bin.shrink_to_fit(); // optional; remove if you prefer stable capacity
    }

    void flushAll(const VulkanContext& ctx) noexcept
    {
        for (auto& bin : m_bins)
        {
            if (bin.empty())
                continue;

            destroyBin(bin, ctx);
            bin.clear();
        }
    }

private:
    static void destroyBin(std::vector<DeferredAsItem>& bin, const VulkanContext& ctx) noexcept
    {
        if (!ctx.device || !ctx.rtDispatch)
        {
            // Still destroy buffers defensively
            for (DeferredAsItem& it : bin)
                it.backing.destroy();
            return;
        }

        for (DeferredAsItem& it : bin)
        {
            if (it.as != VK_NULL_HANDLE)
            {
                ctx.rtDispatch->vkDestroyAccelerationStructureKHR(ctx.device, it.as, nullptr);
                it.as = VK_NULL_HANDLE;
            }

            it.backing.destroy();
        }
    }

private:
    std::vector<std::vector<DeferredAsItem>> m_bins = {};
};
