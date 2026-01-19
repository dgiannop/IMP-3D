//============================================================
// VkRtAccel.cpp
//============================================================
#include "VkRtAccel.hpp"

#include <array>
#include <iostream>

#include "VkUtilities.hpp"

namespace vkrt
{
    // ------------------------------------------------------------
    // Accel destroy helpers
    // ------------------------------------------------------------
    void destroyAccel(const VulkanContext& ctx, RtAccel& a) noexcept
    {
        if (!ctx.device)
            return;

        if (a.handle && ctx.rtDispatch && ctx.rtDispatch->vkDestroyAccelerationStructureKHR)
            ctx.rtDispatch->vkDestroyAccelerationStructureKHR(ctx.device, a.handle, nullptr);

        a.handle  = VK_NULL_HANDLE;
        a.address = 0;
        a.buffer.destroy();
    }

    void destroyTriangleGeom(RtTriangleGeom& g) noexcept
    {
        g.vbo.destroy();
        g.ibo.destroy();
        g.vertexCount = 0;
        g.indexCount  = 0;
        g.vboAddress  = 0;
        g.iboAddress  = 0;
    }

    // ------------------------------------------------------------
    // Main build
    // ------------------------------------------------------------
    bool buildTriangleScene(const VulkanContext& ctx,
                            RtTriangleGeom&      outGeom,
                            RtAccel&             outBlas,
                            RtAccel&             outTlas)
    {
        if (!rtReady(ctx) || !ctx.rtDispatch || !ctx.device)
            return false;

        // Clean any existing
        destroyTriangleGeom(outGeom);
        destroyAccel(ctx, outBlas);
        destroyAccel(ctx, outTlas);

        // Triangle in model space (counter-clockwise)
        struct Vtx
        {
            float x, y, z;
        };

        const std::array<Vtx, 3> verts = {
            Vtx{-0.5f, -0.25f, 0.0f},
            Vtx{+0.5f, -0.25f, 0.0f},
            Vtx{0.0f, +0.5f, 0.0f},
        };

        const std::array<uint32_t, 3> indices = {0, 1, 2};

        // Create device-local VBO/IBO with device addresses
        const VkBufferUsageFlags buildInputUsage =
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

        outGeom.vbo = vkutil::createDeviceLocalBuffer(ctx,
                                                      VkDeviceSize(sizeof(verts)),
                                                      buildInputUsage,
                                                      verts.data(),
                                                      VkDeviceSize(sizeof(verts)),
                                                      /*deviceAddress*/ true);

        outGeom.ibo = vkutil::createDeviceLocalBuffer(ctx,
                                                      VkDeviceSize(sizeof(indices)),
                                                      buildInputUsage,
                                                      indices.data(),
                                                      VkDeviceSize(sizeof(indices)),
                                                      /*deviceAddress*/ true);

        if (!outGeom.vbo.valid() || !outGeom.ibo.valid())
            return false;

        outGeom.vertexCount = 3;
        outGeom.indexCount  = 3;

        outGeom.vboAddress = vkutil::bufferDeviceAddress(ctx.device, outGeom.vbo);
        outGeom.iboAddress = vkutil::bufferDeviceAddress(ctx.device, outGeom.ibo);

        if (outGeom.vboAddress == 0 || outGeom.iboAddress == 0)
        {
            std::cerr << "VkRtAccel: device address failed (buffer_device_address not enabled?).\n";
            return false;
        }

        // ----------------------------
        // BLAS build
        // ----------------------------
        VkAccelerationStructureGeometryTrianglesDataKHR tri{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        tri.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
        tri.vertexData.deviceAddress = outGeom.vboAddress;
        tri.vertexStride             = sizeof(Vtx);
        tri.maxVertex                = outGeom.vertexCount;
        tri.indexType                = VK_INDEX_TYPE_UINT32;
        tri.indexData.deviceAddress  = outGeom.iboAddress;

        VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geom.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geom.flags              = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geom.geometry.triangles = tri;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries   = &geom;

        const uint32_t primCount = 1;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        ctx.rtDispatch->vkGetAccelerationStructureBuildSizesKHR(ctx.device,
                                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                                &buildInfo,
                                                                &primCount,
                                                                &sizeInfo);

        if (sizeInfo.accelerationStructureSize == 0 || sizeInfo.buildScratchSize == 0)
            return false;

        // Allocate BLAS backing buffer
        outBlas.buffer.create(ctx.device,
                              ctx.physicalDevice,
                              sizeInfo.accelerationStructureSize,
                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              /*persistentMap*/ false,
                              /*deviceAddress*/ true);

        if (!outBlas.buffer.valid())
            return false;

        VkAccelerationStructureCreateInfoKHR asci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        asci.size   = sizeInfo.accelerationStructureSize;
        asci.buffer = outBlas.buffer.buffer();

        if (ctx.rtDispatch->vkCreateAccelerationStructureKHR(ctx.device, &asci, nullptr, &outBlas.handle) != VK_SUCCESS)
            return false;

        // Scratch
        GpuBuffer scratch{};
        scratch.create(ctx.device,
                       ctx.physicalDevice,
                       sizeInfo.buildScratchSize,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       /*persistentMap*/ false,
                       /*deviceAddress*/ true);

        if (!scratch.valid())
            return false;

        const VkDeviceAddress scratchAddr = vkutil::bufferDeviceAddress(ctx.device, scratch);
        if (scratchAddr == 0)
            return false;

        buildInfo.dstAccelerationStructure  = outBlas.handle;
        buildInfo.scratchData.deviceAddress = scratchAddr;

        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = primCount;

        const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {&range};

        const bool blasOk = vkutil::TransientCmd(ctx, [&](VkCommandBuffer cmd) {
            ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);

            // Barrier: AS build -> subsequent RT use
            VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 0,
                                 1,
                                 &mb,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr);
        });

        scratch.destroy();

        if (!blasOk)
            return false;

        // Fetch BLAS device address
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        addrInfo.accelerationStructure = outBlas.handle;
        outBlas.address                = ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(ctx.device, &addrInfo);

        if (outBlas.address == 0)
            return false;

        // ----------------------------
        // TLAS build (1 instance)
        // ----------------------------
        VkAccelerationStructureInstanceKHR inst     = {};
        inst.instanceCustomIndex                    = 0;
        inst.mask                                   = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference         = outBlas.address;
        inst.transform                              = VkTransformMatrixKHR{{
            {1, 0, 0, 0},
            {0, 1, 0, 0},
            {0, 0, 1, 0},
        }};

        // Upload instances into a device-addressable buffer
        const VkBufferUsageFlags instUsage =
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

        GpuBuffer instBuf = vkutil::createDeviceLocalBuffer(ctx,
                                                            VkDeviceSize(sizeof(inst)),
                                                            instUsage,
                                                            &inst,
                                                            VkDeviceSize(sizeof(inst)),
                                                            /*deviceAddress*/ true);
        if (!instBuf.valid())
            return false;

        const VkDeviceAddress instAddr = vkutil::bufferDeviceAddress(ctx.device, instBuf);
        if (instAddr == 0)
            return false;

        VkAccelerationStructureGeometryInstancesDataKHR instData{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
        instData.arrayOfPointers    = VK_FALSE;
        instData.data.deviceAddress = instAddr;

        VkAccelerationStructureGeometryKHR tgeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        tgeom.geometryType       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tgeom.geometry.instances = instData;

        VkAccelerationStructureBuildGeometryInfoKHR tbuild{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        tbuild.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tbuild.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        tbuild.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        tbuild.geometryCount = 1;
        tbuild.pGeometries   = &tgeom;

        const uint32_t tprimCount = 1;

        VkAccelerationStructureBuildSizesInfoKHR tsize{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        ctx.rtDispatch->vkGetAccelerationStructureBuildSizesKHR(ctx.device,
                                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                                &tbuild,
                                                                &tprimCount,
                                                                &tsize);

        if (tsize.accelerationStructureSize == 0 || tsize.buildScratchSize == 0)
            return false;

        outTlas.buffer.create(ctx.device,
                              ctx.physicalDevice,
                              tsize.accelerationStructureSize,
                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              /*persistentMap*/ false,
                              /*deviceAddress*/ true);

        if (!outTlas.buffer.valid())
            return false;

        VkAccelerationStructureCreateInfoKHR tasci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        tasci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tasci.size   = tsize.accelerationStructureSize;
        tasci.buffer = outTlas.buffer.buffer();

        if (ctx.rtDispatch->vkCreateAccelerationStructureKHR(ctx.device, &tasci, nullptr, &outTlas.handle) != VK_SUCCESS)
            return false;

        GpuBuffer tscratch{};
        tscratch.create(ctx.device,
                        ctx.physicalDevice,
                        tsize.buildScratchSize,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        /*persistentMap*/ false,
                        /*deviceAddress*/ true);

        if (!tscratch.valid())
            return false;

        const VkDeviceAddress tscratchAddr = vkutil::bufferDeviceAddress(ctx.device, tscratch);
        if (tscratchAddr == 0)
            return false;

        tbuild.dstAccelerationStructure  = outTlas.handle;
        tbuild.scratchData.deviceAddress = tscratchAddr;

        VkAccelerationStructureBuildRangeInfoKHR trange{};
        trange.primitiveCount = tprimCount;

        const VkAccelerationStructureBuildRangeInfoKHR* tranges[] = {&trange};

        const bool tlasOk = vkutil::TransientCmd(ctx, [&](VkCommandBuffer cmd) {
            ctx.rtDispatch->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tbuild, tranges);

            VkMemoryBarrier tmb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            tmb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            tmb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 0,
                                 1,
                                 &tmb,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr);
        });

        tscratch.destroy();
        instBuf.destroy();

        if (!tlasOk)
            return false;

        VkAccelerationStructureDeviceAddressInfoKHR taddrInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        taddrInfo.accelerationStructure = outTlas.handle;
        outTlas.address                 = ctx.rtDispatch->vkGetAccelerationStructureDeviceAddressKHR(ctx.device, &taddrInfo);

        if (outTlas.address == 0)
            return false;

        return true;
    }

} // namespace vkrt
