#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

#include "GpuBuffer.hpp"
#include "VkRtUtils.hpp"
#include "VulkanContext.hpp"

namespace vkrt
{
    /**
     * @brief Small helper owning a Shader Binding Table buffer.
     *
     * Creates a DEVICE_LOCAL, device-addressable buffer.
     * Populates it via staging + vkCmdCopyBuffer.
     *
     * You provide:
     *  - pipeline (RT pipeline)
     *  - group counts (raygen/miss/hit/callable)
     *  - a command pool (transient) and queue for upload
     */
    class RtSbt final
    {
    public:
        RtSbt()  = default;
        ~RtSbt() = default;

        RtSbt(const RtSbt&)            = delete;
        RtSbt& operator=(const RtSbt&) = delete;
        RtSbt(RtSbt&&)                 = default;
        RtSbt& operator=(RtSbt&&)      = default;

        void destroy() noexcept;

        bool buildAndUpload(const VulkanContext& ctx,
                            VkPipeline           rtPipeline,
                            uint32_t             raygenCount,
                            uint32_t             missCount,
                            uint32_t             hitCount,
                            uint32_t             callableCount,
                            VkCommandPool        uploadCmdPool,
                            VkQueue              uploadQueue);

        const vkrt::SbtLayout& layout() const noexcept
        {
            return m_layout;
        }

        VkBuffer buffer() const noexcept
        {
            return m_sbt.buffer();
        }

        VkDeviceAddress deviceAddress(const VulkanContext& ctx) const noexcept
        {
            return vkrt::buffer_device_address(ctx, m_sbt.buffer());
        }

        void regions(const VulkanContext&             ctx,
                     VkStridedDeviceAddressRegionKHR& outRaygen,
                     VkStridedDeviceAddressRegionKHR& outMiss,
                     VkStridedDeviceAddressRegionKHR& outHit,
                     VkStridedDeviceAddressRegionKHR& outCallable) const noexcept
        {
            const VkDeviceAddress base = deviceAddress(ctx);
            vkrt::make_sbt_regions(base, m_layout, outRaygen, outMiss, outHit, outCallable);
        }

    private:
        bool createAndUploadBytes(const VulkanContext&     ctx,
                                  std::span<const uint8_t> bytes,
                                  VkCommandPool            uploadCmdPool,
                                  VkQueue                  uploadQueue);

        bool writeGroupHandles(const VulkanContext&  ctx,
                               VkPipeline            rtPipeline,
                               uint32_t              groupCountTotal,
                               std::vector<uint8_t>& outHandles) const;

        static bool submitAndWait(const VulkanContext& ctx,
                                  VkCommandPool        pool,
                                  VkQueue              queue,
                                  VkCommandBuffer      cmd) noexcept;

    private:
        GpuBuffer       m_sbt    = {};
        vkrt::SbtLayout m_layout = {};
    };

} // namespace vkrt
