//============================================================
// VkUtilities.cpp
//============================================================
#include "VkUtilities.hpp"

#include <iostream>

#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

namespace vkutil
{
    VkTransformMatrixKHR toVkTransformMatrix(const glm::mat4& m) noexcept
    {
        VkTransformMatrixKHR t = {};

        t.matrix[0][0] = m[0][0];
        t.matrix[0][1] = m[1][0];
        t.matrix[0][2] = m[2][0];
        t.matrix[0][3] = m[3][0];

        t.matrix[1][0] = m[0][1];
        t.matrix[1][1] = m[1][1];
        t.matrix[1][2] = m[2][1];
        t.matrix[1][3] = m[3][1];

        t.matrix[2][0] = m[0][2];
        t.matrix[2][1] = m[1][2];
        t.matrix[2][2] = m[2][2];
        t.matrix[2][3] = m[3][2];

        return t;
    }

    VkClearColorValue toVkClearColor(const glm::vec4& color) noexcept
    {
        VkClearColorValue v = {};
        v.float32[0]        = color.r;
        v.float32[1]        = color.g;
        v.float32[2]        = color.b;
        v.float32[3]        = color.a;
        return v;
    }

    void printVkResult(VkResult r, const char* where)
    {
        const char* name;
        switch (r)
        {
            case VK_SUCCESS:
                name = "VK_SUCCESS";
                break;
            case VK_TIMEOUT:
                name = "VK_TIMEOUT";
                break;
            case VK_ERROR_DEVICE_LOST:
                name = "VK_ERROR_DEVICE_LOST";
                break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                name = "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                break;
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                name = "VK_ERROR_OUT_OF_HOST_MEMORY";
                break;
            case VK_ERROR_INITIALIZATION_FAILED:
                name = "VK_ERROR_INITIALIZATION_FAILED";
                break;
            case VK_ERROR_OUT_OF_DATE_KHR:
                name = "VK_ERROR_OUT_OF_DATE_KHR";
                break;
            case VK_SUBOPTIMAL_KHR:
                name = "VK_SUBOPTIMAL_KHR";
                break;
            case VK_ERROR_NOT_ENOUGH_SPACE_KHR:
                name = "VK_ERROR_NOT_ENOUGH_SPACE_KHR";
                break;
            default:
                name = "VK_UNDEFINED";
                break;
        }

        std::cerr << "[Vulkan] " << where << " -> " << name << " (" << int(r) << ")\n";
    }

    void imageBarrier(VkCommandBuffer      cmd,
                      VkImage              image,
                      VkImageLayout        oldLayout,
                      VkImageLayout        newLayout,
                      VkAccessFlags        srcAccess,
                      VkAccessFlags        dstAccess,
                      VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage,
                      VkImageAspectFlags   aspectMask,
                      uint32_t             baseMip,
                      uint32_t             mipCount,
                      uint32_t             baseLayer,
                      uint32_t             layerCount) noexcept
    {
        if (!cmd || image == VK_NULL_HANDLE)
            return;

        assert(mipCount != 0 && layerCount != 0);

        VkImageMemoryBarrier b{};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                       = oldLayout;
        b.newLayout                       = newLayout;
        b.srcAccessMask                   = srcAccess;
        b.dstAccessMask                   = dstAccess;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = image;
        b.subresourceRange.aspectMask     = aspectMask;
        b.subresourceRange.baseMipLevel   = baseMip;
        b.subresourceRange.levelCount     = mipCount;
        b.subresourceRange.baseArrayLayer = baseLayer;
        b.subresourceRange.layerCount     = layerCount;

        vkCmdPipelineBarrier(cmd,
                             srcStage,
                             dstStage,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &b);
    }

    // ============================================================================
    // Upload helpers (frame-cmd path)
    // ============================================================================

    bool ensureUploadBuffer(const VulkanContext& ctx,
                            GpuBuffer&           upload,
                            VkDeviceSize         bytes) noexcept
    {
        if (!ctx.device || !ctx.physicalDevice || bytes == 0)
            return false;

        if (upload.valid() && upload.size() >= bytes)
            return true;

        upload.destroy();
        upload.create(ctx.device,
                      ctx.physicalDevice,
                      bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      /*persistentMap*/ true);

        return upload.valid();
    }

    namespace
    {
        // Intentionally leak staging buffers for debugging:
        // If this fixes the hang/device-lost, the bug is staging lifetime (destroyed too early / reused too soon).
        static std::vector<GpuBuffer>& leakedStagingBuffers()
        {
            static std::vector<GpuBuffer> s_leak;
            return s_leak;
        }

        static void leakStaging(GpuBuffer&& b)
        {
            if (!b.valid())
                return;

            auto& v = leakedStagingBuffers();
            v.push_back(std::move(b));
        }
    } // namespace

    namespace vkutil
    {

        bool recordUploadToDeviceLocalBuffer(const VulkanContext& ctx,
                                             VkCommandBuffer      cmd,
                                             VkBuffer             dst,
                                             VkDeviceSize         dstOffset,
                                             const void*          data,
                                             VkDeviceSize         bytes) noexcept
        {
            if (!cmd || !dst || !data || bytes == 0)
                return false;

            FrameUploadTrash* trash = frameUploadTrash();
            if (!trash)
            {
                // If you want: assert(false) here so you catch missing binding immediately.
                std::cerr << "vkutil::recordUploadToDeviceLocalBuffer: no FrameUploadTrash bound.\n";
                return false;
            }

            GpuBuffer staging;
            staging.create(ctx.device,
                           ctx.physicalDevice,
                           bytes,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           /*persistentMap*/ true);

            if (!staging.valid())
                return false;

            staging.upload(data, bytes);

            VkBufferCopy cpy{};
            cpy.srcOffset = 0;
            cpy.dstOffset = dstOffset;
            cpy.size      = bytes;

            vkCmdCopyBuffer(cmd, staging.buffer(), dst, 1, &cpy);

            // Keep staging alive until the fence for this frame signals.
            trash->staging.push_back(std::move(staging));
            return true;
        }
    } // namespace vkutil

    GpuBuffer createDeviceLocalBufferEmpty(const VulkanContext& ctx,
                                           VkDeviceSize         capacity,
                                           VkBufferUsageFlags   usage,
                                           bool                 deviceAddress)
    {
        GpuBuffer buffer = {};

        if (!ctx.device || !ctx.physicalDevice || capacity == 0)
            return buffer;

        // IMPORTANT: you will upload into this, so it must be TRANSFER_DST.
        VkBufferUsageFlags finalUsage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (deviceAddress)
            finalUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        buffer.create(ctx.device,
                      ctx.physicalDevice,
                      capacity,
                      finalUsage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      /*persistentMap*/ false,
                      /*deviceAddress*/ deviceAddress);

        return buffer;
    }

    // ============================================================================
    // Barriers (explicit, legal, no mega-barriers)
    // ============================================================================

    void barrierTransferToVertexAttributeRead(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb = {};
        mb.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask   = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    void barrierTransferToIndexRead(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb = {};
        mb.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask   = VK_ACCESS_INDEX_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    void barrierTransferToGraphicsShaderRead(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb = {};
        mb.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    void barrierTransferToRtShaderRead(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb = {};
        mb.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    // --------------------------------------------------------------------------
    // NEW: Transfer-write -> AS build read (build input buffers)
    //
    // Placement: barrier helpers block, right next to other Transfer->X helpers.
    //
    // Use after uploading buffers that are consumed by AS build commands:
    //  - BLAS geometry vertex/index buffers
    //  - TLAS instance buffers
    // --------------------------------------------------------------------------
    void barrierTransferToAsBuildRead(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb = {};
        mb.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    void barrierAsBuildToTrace(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb = {};
        mb.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

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
    }

    // ============================================================================
    // Device address helpers
    // ============================================================================

    VkDeviceAddress bufferDeviceAddress(VkDevice device, VkBuffer buf) noexcept
    {
        VkBufferDeviceAddressInfo info = {};
        info.sType                     = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer                    = buf;
        return vkGetBufferDeviceAddress(device, &info);
    }

    VkDeviceAddress bufferDeviceAddress(VkDevice device, const GpuBuffer& buf) noexcept
    {
        return bufferDeviceAddress(device, buf.buffer());
    }

    uint32_t findMemoryType(VkPhysicalDevice      phys,
                            uint32_t              typeBits,
                            VkMemoryPropertyFlags props) noexcept
    {
        VkPhysicalDeviceMemoryProperties mp = {};
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);

        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        {
            const bool supported = (typeBits & (1u << i)) != 0u;
            const bool matches   = (mp.memoryTypes[i].propertyFlags & props) == props;

            if (supported && matches)
                return i;
        }

        return UINT32_MAX;
    }

    // ============================================================================
    // Fixed function helpers
    // ============================================================================

    VkPipelineInputAssemblyStateCreateInfo makeInputAssembly(VkPrimitiveTopology topology)
    {
        VkPipelineInputAssemblyStateCreateInfo ia = {};
        ia.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology                               = topology;
        ia.primitiveRestartEnable                 = VK_FALSE;
        return ia;
    }

    VkPipelineViewportStateCreateInfo makeViewportState()
    {
        VkPipelineViewportStateCreateInfo vp = {};
        vp.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount                     = 1;
        vp.scissorCount                      = 1;
        return vp;
    }

    VkPipelineRasterizationStateCreateInfo makeRasterState(VkCullModeFlags cullMode,
                                                           VkPolygonMode   mode,
                                                           VkFrontFace     frontFace,
                                                           float           lineWidth)
    {
        VkPipelineRasterizationStateCreateInfo rs = {};
        rs.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.depthClampEnable                       = VK_FALSE;
        rs.rasterizerDiscardEnable                = VK_FALSE;
        rs.polygonMode                            = mode;
        rs.cullMode                               = cullMode;
        rs.frontFace                              = frontFace;
        rs.depthBiasEnable                        = VK_FALSE;
        rs.lineWidth                              = lineWidth;
        return rs;
    }

    VkPipelineMultisampleStateCreateInfo makeMultisampleState(VkSampleCountFlagBits samples)
    {
        VkPipelineMultisampleStateCreateInfo ms = {};
        ms.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples                 = samples;
        ms.sampleShadingEnable                  = VK_FALSE;
        return ms;
    }

    VkPipelineDepthStencilStateCreateInfo makeDepthStencilState(bool depthWriteEnable)
    {
        VkPipelineDepthStencilStateCreateInfo ds = {};
        ds.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable                       = VK_TRUE;
        ds.depthWriteEnable                      = depthWriteEnable ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp                        = VK_COMPARE_OP_LESS;
        ds.depthBoundsTestEnable                 = VK_FALSE;
        ds.stencilTestEnable                     = VK_FALSE;
        return ds;
    }

    VkPipelineColorBlendAttachmentState makeColorBlendAttachment(bool enableBlend)
    {
        VkPipelineColorBlendAttachmentState att = {};
        att.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT |
                             VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT |
                             VK_COLOR_COMPONENT_A_BIT;

        att.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;

        if (enableBlend)
        {
            att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.colorBlendOp        = VK_BLEND_OP_ADD;
            att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.alphaBlendOp        = VK_BLEND_OP_ADD;
        }

        return att;
    }

    VkPipelineColorBlendStateCreateInfo makeColorBlendState(const VkPipelineColorBlendAttachmentState* attachment)
    {
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable                       = VK_FALSE;
        cb.attachmentCount                     = 1;
        cb.pAttachments                        = attachment;
        return cb;
    }

    VkPipelineDynamicStateCreateInfo makeDynamicState(const VkDynamicState* states, uint32_t count)
    {
        VkPipelineDynamicStateCreateInfo dyn = {};
        dyn.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount                = count;
        dyn.pDynamicStates                   = states;
        return dyn;
    }

    void setViewportAndScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height)
    {
        VkViewport vp = {};
        vp.x          = 0.0f;
        vp.y          = 0.0f;
        vp.width      = static_cast<float>(width);
        vp.height     = static_cast<float>(height);
        vp.minDepth   = 0.0f;
        vp.maxDepth   = 1.0f;

        VkRect2D sc = {};
        sc.offset   = {0, 0};
        sc.extent   = {width, height};

        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    // ============================================================================
    // One-time command buffer helpers (unchanged, but now not used for per-frame updates)
    // ============================================================================

    OneTimeCmd beginTransientCmd(const VulkanContext& ctx)
    {
        OneTimeCmd otc  = {};
        otc.device      = ctx.device;
        otc.queue       = ctx.graphicsQueue;
        otc.queueFamily = ctx.graphicsQueueFamilyIndex;

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex        = otc.queueFamily;

        if (vkCreateCommandPool(otc.device, &poolInfo, nullptr, &otc.pool) != VK_SUCCESS)
            return otc;

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool                 = otc.pool;
        allocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount          = 1;

        if (vkAllocateCommandBuffers(otc.device, &allocInfo, &otc.cmd) != VK_SUCCESS)
        {
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            otc.pool = VK_NULL_HANDLE;
            return otc;
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(otc.cmd, &beginInfo) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            otc.cmd  = VK_NULL_HANDLE;
            otc.pool = VK_NULL_HANDLE;
            return otc;
        }

        return otc;
    }

    bool submitTransientCmd(const OneTimeCmd& otc) noexcept
    {
        if (!otc.device || !otc.pool || !otc.cmd || !otc.queue)
            return false;

        if (vkEndCommandBuffer(otc.cmd) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            return false;
        }

        VkFenceCreateInfo fci = {};
        fci.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(otc.device, &fci, nullptr, &fence) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            return false;
        }

        VkSubmitInfo submitInfo       = {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &otc.cmd;

        const VkResult sr = vkQueueSubmit(otc.queue, 1, &submitInfo, fence);
        if (sr != VK_SUCCESS)
        {
            printVkResult(sr, "vkQueueSubmit (TransientCmd)");
            vkDestroyFence(otc.device, fence, nullptr);
            vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            return false;
        }

        const VkResult wr = vkWaitForFences(otc.device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (wr != VK_SUCCESS)
        {
            printVkResult(wr, "vkWaitForFences (TransientCmd)");
            vkDestroyFence(otc.device, fence, nullptr);
            return false;
        }

        vkDestroyFence(otc.device, fence, nullptr);

        // Debug-only; keep if you want, but consider removing later.
        const VkResult qr = vkQueueWaitIdle(otc.queue);
        if (qr != VK_SUCCESS)
        {
            printVkResult(qr, "vkQueueWaitIdle (TransientCmd)");
            return false;
        }

        vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
        vkDestroyCommandPool(otc.device, otc.pool, nullptr);
        return true;
    }

    bool TransientCmd(const VulkanContext& ctx, const std::function<void(VkCommandBuffer)>& record)
    {
        OneTimeCmd otc = beginTransientCmd(ctx);
        if (!otc.device || !otc.pool || !otc.cmd || !otc.queue)
            return false;

        record(otc.cmd);
        return submitTransientCmd(otc);
    }

    // ============================================================================
    // Legacy device-local helpers (still transient). Keep for now.
    // ============================================================================

    GpuBuffer createDeviceLocalBuffer(const VulkanContext& ctx,
                                      VkDeviceSize         capacity,
                                      VkBufferUsageFlags   usage,
                                      const void*          data,
                                      VkDeviceSize         copySize,
                                      bool                 deviceAddress)
    {
        GpuBuffer dst;

        dst.create(ctx.device,
                   ctx.physicalDevice,
                   capacity,
                   usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   /*persistentMap*/ false,
                   /*deviceAddress*/ deviceAddress);

        if (!dst.valid())
        {
            std::cerr << "vkutil::createDeviceLocalBuffer(capacity): dst.create failed.\n";
            return {};
        }

        GpuBuffer staging;
        staging.create(ctx.device,
                       ctx.physicalDevice,
                       copySize,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       /*persistentMap*/ true);

        if (!staging.valid())
        {
            std::cerr << "vkutil::createDeviceLocalBuffer(capacity): staging.create failed.\n";
            dst.destroy();
            return {};
        }

        staging.upload(data, copySize);

        bool ok = TransientCmd(ctx, [&](VkCommandBuffer cmd) {
            VkBufferCopy copy = {};
            copy.srcOffset    = 0;
            copy.dstOffset    = 0;
            copy.size         = copySize;
            vkCmdCopyBuffer(cmd, staging.buffer(), dst.buffer(), 1, &copy);
        });

        if (!ok)
        {
            std::cerr << "vkutil::createDeviceLocalBuffer(capacity): submitOneTimeCommands failed.\n";
            dst.destroy();
            return {};
        }

        return dst;
    }

    void updateDeviceLocalBuffer(const VulkanContext& ctx,
                                 const GpuBuffer&     dst,
                                 VkDeviceSize         dstOffset,
                                 VkDeviceSize         size,
                                 const void*          data)
    {
        if (!dst.valid() || size == 0 || data == nullptr)
            return;

        GpuBuffer staging;
        staging.create(ctx.device,
                       ctx.physicalDevice,
                       size,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       /*persistentMap*/ true);

        if (!staging.valid())
        {
            std::cerr << "vkutil::updateDeviceLocalBuffer: staging.create failed.\n";
            return;
        }

        staging.upload(data, size);

        bool ok = TransientCmd(ctx, [&](VkCommandBuffer cmd) {
            VkBufferCopy copy = {};
            copy.srcOffset    = 0;
            copy.dstOffset    = dstOffset;
            copy.size         = size;
            vkCmdCopyBuffer(cmd, staging.buffer(), dst.buffer(), 1, &copy);
        });

        if (!ok)
        {
            std::cerr << "vkutil::updateDeviceLocalBuffer: submitOneTimeCommands failed.\n";
        }

        // DEBUG: keep staging alive forever
        leakStaging(std::move(staging));
    }

    // ============================================================================
    // Pipeline helper
    // ============================================================================

    VkPipeline createGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& d)
    {
        VkGraphicsPipelineCreateInfo ci = {};
        ci.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount                   = d.stageCount;
        ci.pStages                      = d.stages;
        ci.pVertexInputState            = d.vertexInput;
        ci.pInputAssemblyState          = d.inputAssembly;
        ci.pViewportState               = d.viewport;
        ci.pRasterizationState          = d.rasterization;
        ci.pMultisampleState            = d.multisample;
        ci.pDepthStencilState           = d.depthStencil;
        ci.pColorBlendState             = d.colorBlend;
        ci.pDynamicState                = d.dynamicState;
        ci.layout                       = d.layout;
        ci.renderPass                   = d.renderPass;
        ci.subpass                      = d.subpass;
        ci.basePipelineHandle           = VK_NULL_HANDLE;
        ci.basePipelineIndex            = -1;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &ci,
                                      nullptr,
                                      &pipeline) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }

        return pipeline;
    }

    bool recordUploadToDeviceLocalBuffer(const VulkanContext& ctx,
                                         VkCommandBuffer      cmd,
                                         GpuBuffer& /*upload*/,
                                         VkBuffer     dst,
                                         VkDeviceSize dstOffset,
                                         const void*  data,
                                         VkDeviceSize bytes) noexcept
    {
        if (!cmd || !dst || !data || bytes == 0)
            return false;

        GpuBuffer staging;
        staging.create(ctx.device,
                       ctx.physicalDevice,
                       bytes,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       /*persistentMap*/ true);

        if (!staging.valid())
            return false;

        staging.upload(data, bytes);

        VkBufferCopy cpy{};
        cpy.srcOffset = 0;
        cpy.dstOffset = dstOffset;
        cpy.size      = bytes;

        vkCmdCopyBuffer(cmd, staging.buffer(), dst, 1, &cpy);

        // DEBUG: keep staging alive forever
        leakStaging(std::move(staging));
        return true;
    }

} // namespace vkutil

namespace vkutil
{
    static thread_local FrameUploadTrash* g_frameTrash = nullptr;

    void setFrameUploadTrash(FrameUploadTrash* trash) noexcept
    {
        g_frameTrash = trash;
    }

    FrameUploadTrash* frameUploadTrash() noexcept
    {
        return g_frameTrash;
    }
} // namespace vkutil
