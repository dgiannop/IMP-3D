//============================================================
// VkRtAccel.hpp
//============================================================
#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

namespace vkrt
{
    struct RtAccel final
    {
        VkAccelerationStructureKHR handle  = VK_NULL_HANDLE;
        GpuBuffer                  buffer  = {};
        VkDeviceAddress            address = 0;
    };

    struct RtTriangleGeom final
    {
        GpuBuffer       vbo         = {}; // DEVICE_LOCAL + device addressable
        GpuBuffer       ibo         = {}; // DEVICE_LOCAL + device addressable
        uint32_t        vertexCount = 0;
        uint32_t        indexCount  = 0;
        VkDeviceAddress vboAddress  = 0;
        VkDeviceAddress iboAddress  = 0;
    };

    /// Builds:
    ///  - vertex/index buffers for a single triangle
    ///  - BLAS from that triangle
    ///  - TLAS with one instance of the BLAS
    ///
    /// Uses vkutil transient command submission (fence wait) on ctx.graphicsQueue.
    bool buildTriangleScene(const VulkanContext& ctx,
                            RtTriangleGeom&      outGeom,
                            RtAccel&             outBlas,
                            RtAccel&             outTlas);

    void destroyAccel(const VulkanContext& ctx, RtAccel& a) noexcept;
    void destroyTriangleGeom(RtTriangleGeom& g) noexcept;

} // namespace vkrt
