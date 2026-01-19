#include "VkUtilities.hpp"

#include <iostream>

#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

namespace vkutil
{
    VkTransformMatrixKHR toVkTransformMatrix(const glm::mat4& m) noexcept
    {
        VkTransformMatrixKHR t = {};

        // VkTransformMatrixKHR is a 3x4 row-major matrix.
        // GLM is column-major. The mapping you used is correct.
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
        VkClearColorValue v{};
        v.float32[0] = color.r;
        v.float32[1] = color.g;
        v.float32[2] = color.b;
        v.float32[3] = color.a;
        return v;
    }

    void printVkResult(VkResult r, const char* where)
    {
        const char* name = "VK_<unknown>";
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

        std::cerr << "[Vulkan] " << where << " -> "
                  << name << " (" << int(r) << ")\n";
    }

    VkDeviceAddress bufferDeviceAddress(VkDevice device, VkBuffer buf) noexcept
    {
        VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        info.buffer = buf;
        return vkGetBufferDeviceAddress(device, &info);
    }

    // Overload if your GpuBuffer type exists:
    VkDeviceAddress bufferDeviceAddress(VkDevice device, const GpuBuffer& buf) noexcept
    {
        return bufferDeviceAddress(device, buf.buffer());
    }

    // VkDeviceAddress bufferDeviceAddress(const VulkanContext& ctx, VkBuffer buf)
    // {
    //     VkBufferDeviceAddressInfo info{};
    //     info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    //     info.buffer = buf;
    //     return vkGetBufferDeviceAddress(ctx.device, &info);
    // }

    uint32_t findMemoryType(VkPhysicalDevice      phys,
                            uint32_t              typeBits,
                            VkMemoryPropertyFlags props) noexcept
    {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);

        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        {
            bool supported = typeBits & (1u << i);
            bool matches   = (mp.memoryTypes[i].propertyFlags & props) == props;

            if (supported && matches)
                return i;
        }

        // Very rarely happens, return a sentinel
        return UINT32_MAX;
    }

    VkPipelineInputAssemblyStateCreateInfo makeInputAssembly(VkPrimitiveTopology topology)
    {
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = topology;
        ia.primitiveRestartEnable = VK_FALSE;
        return ia;
    }

    VkPipelineViewportStateCreateInfo makeViewportState()
    {
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;
        return vp;
    }

    VkPipelineRasterizationStateCreateInfo makeRasterState(VkCullModeFlags cullMode,
                                                           VkPolygonMode   mode,
                                                           VkFrontFace     frontFace,
                                                           float           lineWidth)
    {
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.depthClampEnable        = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode             = mode;
        rs.cullMode                = cullMode;
        rs.frontFace               = frontFace;
        rs.depthBiasEnable         = VK_FALSE;
        rs.lineWidth               = lineWidth;
        return rs;
    }

    VkPipelineMultisampleStateCreateInfo makeMultisampleState(VkSampleCountFlagBits samples)
    {
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = samples;
        ms.sampleShadingEnable  = VK_FALSE;
        return ms;
    }

    VkPipelineDepthStencilStateCreateInfo makeDepthStencilState(bool depthWriteEnable)
    {
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable       = VK_TRUE;
        ds.depthWriteEnable      = depthWriteEnable ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp        = VK_COMPARE_OP_LESS;
        ds.depthBoundsTestEnable = VK_FALSE;
        ds.stencilTestEnable     = VK_FALSE;
        return ds;
    }

    VkPipelineColorBlendAttachmentState makeColorBlendAttachment(bool enableBlend)
    {
        VkPipelineColorBlendAttachmentState att{};
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
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
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable   = VK_FALSE;
        cb.attachmentCount = 1;
        cb.pAttachments    = attachment;
        return cb;
    }

    VkPipelineDynamicStateCreateInfo makeDynamicState(const VkDynamicState* states, uint32_t count)
    {
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = count;
        dyn.pDynamicStates    = states;
        return dyn;
    }

    void setViewportAndScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height)
    {
        VkViewport vp{};
        vp.x        = 0.0f;
        vp.y        = 0.0f;
        vp.width    = static_cast<float>(width);
        vp.height   = static_cast<float>(height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;

        VkRect2D sc{};
        sc.offset = {0, 0};
        sc.extent = {width, height};

        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    // -------------------------------------------------------------
    // One-time command buffer helpers
    // -------------------------------------------------------------

    vkutil::OneTimeCmd beginTransientCmd(const VulkanContext& ctx)
    {
        OneTimeCmd otc{};
        otc.device      = ctx.device;
        otc.queue       = ctx.graphicsQueue;
        otc.queueFamily = ctx.graphicsQueueFamilyIndex;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = otc.queueFamily;

        if (vkCreateCommandPool(otc.device, &poolInfo, nullptr, &otc.pool) != VK_SUCCESS)
            return otc;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = otc.pool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(otc.device, &allocInfo, &otc.cmd) != VK_SUCCESS)
        {
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            otc.pool = VK_NULL_HANDLE;
            return otc;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

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

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(otc.device, &fci, nullptr, &fence) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &otc.cmd;

        const VkResult sr = vkQueueSubmit(otc.queue, 1, &submitInfo, fence);
        if (sr != VK_SUCCESS)
        {
            vkutil::printVkResult(sr, "vkQueueSubmit (TransientCmd)");
            vkDestroyFence(otc.device, fence, nullptr);
            vkFreeCommandBuffers(otc.device, otc.pool, 1, &otc.cmd);
            vkDestroyCommandPool(otc.device, otc.pool, nullptr);
            return false;
        }

        const VkResult wr = vkWaitForFences(otc.device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (wr != VK_SUCCESS)
        {
            vkutil::printVkResult(wr, "vkWaitForFences (TransientCmd)");

            std::cerr << "submitTransientCmd: vkWaitForFences failed: " << wr << "\n";
            // Do NOT free cmd buffers / destroy pool here; device is likely lost or GPU hung.
            vkDestroyFence(otc.device, fence, nullptr);
            return false;
        }

        vkDestroyFence(otc.device, fence, nullptr);

        // Optional (debug-only). Fence already guarantees completion of *that* submit.
        const VkResult qr = vkQueueWaitIdle(otc.queue); // todo: remove this in normal code
        if (qr != VK_SUCCESS)
        {
            printVkResult(qr, "vkQueueWaitIdle (TransientCmd)");
            std::cerr << "submitTransientCmd: vkQueueWaitIdle failed: " << qr << "\n";
            return false; // again, don't free cmd/pool on failure
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

    // -------------------------------------------------------------
    // Buffer upload helpers
    // -------------------------------------------------------------

    GpuBuffer createDeviceLocalBuffer(const VulkanContext& ctx,
                                      VkDeviceSize         capacity,
                                      VkBufferUsageFlags   usage,
                                      const void*          data,
                                      VkDeviceSize         copySize,
                                      bool                 deviceAddress)
    {
        GpuBuffer dst;

        // Device-local buffer with 'capacity' bytes
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

        // Staging buffer only needs to hold 'copySize' bytes
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
            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = 0;
            copy.size      = copySize;
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

    GpuBuffer createDeviceLocalBuffer(const VulkanContext& ctx,
                                      VkDeviceSize         size,
                                      VkBufferUsageFlags   usage,
                                      const void*          data)
    {
        // Default: capacity == copySize == size (old behavior)
        return createDeviceLocalBuffer(ctx, size, usage, data, size);
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
            std::cerr << "vkutil::updateDeviceLocalBuffer: staging.create failed." << std::endl;
            return;
        }

        staging.upload(data, size);

        bool ok = TransientCmd(ctx, [&](VkCommandBuffer cmd) {
            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = dstOffset;
            copy.size      = size;
            vkCmdCopyBuffer(cmd, staging.buffer(), dst.buffer(), 1, &copy);
        });

        if (!ok)
        {
            std::cerr << "vkutil::updateDeviceLocalBuffer: submitOneTimeCommands failed." << std::endl;
        }
    }

} // namespace vkutil

namespace vkutil
{

    VkPipeline createGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& d)
    {
        VkGraphicsPipelineCreateInfo ci{};
        ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount          = d.stageCount;
        ci.pStages             = d.stages;
        ci.pVertexInputState   = d.vertexInput;
        ci.pInputAssemblyState = d.inputAssembly;
        ci.pViewportState      = d.viewport;
        ci.pRasterizationState = d.rasterization;
        ci.pMultisampleState   = d.multisample;
        ci.pDepthStencilState  = d.depthStencil;
        ci.pColorBlendState    = d.colorBlend;
        ci.pDynamicState       = d.dynamicState;
        ci.layout              = d.layout;
        ci.renderPass          = d.renderPass;
        ci.subpass             = d.subpass;
        ci.basePipelineHandle  = VK_NULL_HANDLE;
        ci.basePipelineIndex   = -1;

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

} // namespace vkutil
