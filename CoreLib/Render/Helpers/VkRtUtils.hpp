#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

#include "VulkanContext.hpp"

/**
 * @file VkRtUtils.hpp
 * @brief CoreLib ray tracing helpers (dispatch + device address + SBT layout).
 *
 * This module assumes the UI/backend layer provides VulkanContext with:
 *  - supportsRayTracing == true
 *  - rtDispatch != nullptr
 *  - rtProps/asProps filled
 */

namespace vkrt
{
    // ------------------------------------------------------------
    // Basics
    // ------------------------------------------------------------

    template<typename T>
    inline T align_up(T v, T a) noexcept
    {
        if (a == T(0))
            return v;
        return (v + (a - T(1))) / a * a;
    }

    inline uint32_t handle_size(const VulkanContext& ctx) noexcept
    {
        return ctx.rtProps.shaderGroupHandleSize;
    }

    inline uint32_t handle_size_aligned(const VulkanContext& ctx) noexcept
    {
        // Spec: handle size must be aligned to shaderGroupHandleAlignment when stored in SBT.
        const uint32_t h  = ctx.rtProps.shaderGroupHandleSize;
        const uint32_t ha = ctx.rtProps.shaderGroupHandleAlignment;
        return align_up<uint32_t>(h, ha);
    }

    // ------------------------------------------------------------
    // Buffer Device Address
    // ------------------------------------------------------------

    inline VkDeviceAddress buffer_device_address(const VulkanContext& ctx, VkBuffer buffer) noexcept
    {
        if (!rtReady(ctx) || !buffer)
            return 0;

        VkBufferDeviceAddressInfo info = {};
        info.sType                     = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.pNext                     = nullptr;
        info.buffer                    = buffer;

        return ctx.rtDispatch->vkGetBufferDeviceAddressKHR(ctx.device, &info);
    }

    // ------------------------------------------------------------
    // Shader Binding Table layout
    // ------------------------------------------------------------

    struct SbtLayout
    {
        // Required handle sizes
        uint32_t handleSize        = 0;
        uint32_t handleSizeAligned = 0;

        // Group counts used to compute layout
        uint32_t raygenCount   = 0;
        uint32_t missCount     = 0;
        uint32_t hitCount      = 0;
        uint32_t callableCount = 0;

        // Strides (each record stride must be aligned to shaderGroupBaseAlignment)
        VkDeviceSize raygenStride   = 0;
        VkDeviceSize missStride     = 0;
        VkDeviceSize hitStride      = 0;
        VkDeviceSize callableStride = 0;

        // Sizes (stride * count; raygen often equals stride because count is 1)
        VkDeviceSize raygenSize   = 0;
        VkDeviceSize missSize     = 0;
        VkDeviceSize hitSize      = 0;
        VkDeviceSize callableSize = 0;

        // Offsets from start of the SBT buffer
        VkDeviceSize raygenOffset   = 0;
        VkDeviceSize missOffset     = 0;
        VkDeviceSize hitOffset      = 0;
        VkDeviceSize callableOffset = 0;

        // Total SBT buffer size (aligned as needed)
        VkDeviceSize totalSize = 0;
    };

    /**
     * @brief Compute SBT offsets/strides/sizes for the given group counts.
     *
     * Notes:
     *  - Records contain at least the shader handle. If you later append inline parameters
     *    after each handle, pass recordDataSizeX > 0 and the stride will grow accordingly.
     *  - Vulkan requires each region stride to be a multiple of shaderGroupBaseAlignment.
     */
    inline SbtLayout compute_sbt_layout(const VulkanContext& ctx,
                                        uint32_t             raygenCount,
                                        uint32_t             missCount,
                                        uint32_t             hitCount,
                                        uint32_t             callableCount,
                                        uint32_t             raygenRecordDataSize = 0,
                                        uint32_t             missRecordDataSize   = 0,
                                        uint32_t             hitRecordDataSize    = 0,
                                        uint32_t             callRecordDataSize   = 0) noexcept
    {
        SbtLayout out = {};
        if (!rtReady(ctx))
            return out;

        out.handleSize        = handle_size(ctx);
        out.handleSizeAligned = handle_size_aligned(ctx);

        out.raygenCount   = raygenCount;
        out.missCount     = missCount;
        out.hitCount      = hitCount;
        out.callableCount = callableCount;

        const VkDeviceSize baseAlign = VkDeviceSize(ctx.rtProps.shaderGroupBaseAlignment);

        // Each record: [aligned handle bytes] + [optional inline data]
        const VkDeviceSize rgRec = VkDeviceSize(out.handleSizeAligned + raygenRecordDataSize);
        const VkDeviceSize msRec = VkDeviceSize(out.handleSizeAligned + missRecordDataSize);
        const VkDeviceSize htRec = VkDeviceSize(out.handleSizeAligned + hitRecordDataSize);
        const VkDeviceSize clRec = VkDeviceSize(out.handleSizeAligned + callRecordDataSize);

        out.raygenStride   = align_up<VkDeviceSize>(rgRec, baseAlign);
        out.missStride     = align_up<VkDeviceSize>(msRec, baseAlign);
        out.hitStride      = align_up<VkDeviceSize>(htRec, baseAlign);
        out.callableStride = align_up<VkDeviceSize>(clRec, baseAlign);

        out.raygenSize   = out.raygenStride * VkDeviceSize(raygenCount);
        out.missSize     = out.missStride * VkDeviceSize(missCount);
        out.hitSize      = out.hitStride * VkDeviceSize(hitCount);
        out.callableSize = out.callableStride * VkDeviceSize(callableCount);

        // Offsets: keep each region start aligned to shaderGroupBaseAlignment.
        out.raygenOffset = 0;

        out.missOffset = align_up<VkDeviceSize>(out.raygenOffset + out.raygenSize, baseAlign);
        out.hitOffset  = align_up<VkDeviceSize>(out.missOffset + out.missSize, baseAlign);
        out.callableOffset =
            align_up<VkDeviceSize>(out.hitOffset + out.hitSize, baseAlign);

        out.totalSize =
            align_up<VkDeviceSize>(out.callableOffset + out.callableSize, baseAlign);

        return out;
    }

    /**
     * @brief Build VkStridedDeviceAddressRegionKHR structs for vkCmdTraceRaysKHR.
     *
     * @param sbtBaseAddress Device address of the start of the SBT buffer.
     * @param layout Layout previously computed by compute_sbt_layout().
     */
    inline void make_sbt_regions(VkDeviceAddress                  sbtBaseAddress,
                                 const SbtLayout&                 layout,
                                 VkStridedDeviceAddressRegionKHR& outRaygen,
                                 VkStridedDeviceAddressRegionKHR& outMiss,
                                 VkStridedDeviceAddressRegionKHR& outHit,
                                 VkStridedDeviceAddressRegionKHR& outCallable) noexcept
    {
        outRaygen   = {};
        outMiss     = {};
        outHit      = {};
        outCallable = {};

        if (sbtBaseAddress == 0 || layout.totalSize == 0)
            return;

        outRaygen.deviceAddress = sbtBaseAddress + layout.raygenOffset;
        outRaygen.stride        = layout.raygenStride;
        outRaygen.size          = layout.raygenSize;

        outMiss.deviceAddress = sbtBaseAddress + layout.missOffset;
        outMiss.stride        = layout.missStride;
        outMiss.size          = layout.missSize;

        outHit.deviceAddress = sbtBaseAddress + layout.hitOffset;
        outHit.stride        = layout.hitStride;
        outHit.size          = layout.hitSize;

        outCallable.deviceAddress = sbtBaseAddress + layout.callableOffset;
        outCallable.stride        = layout.callableStride;
        outCallable.size          = layout.callableSize;
    }

} // namespace vkrt
