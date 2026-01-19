#include "RtSbt.hpp"

#include <cstring>

namespace vkrt
{
    void RtSbt::destroy() noexcept
    {
        m_sbt.destroy();
        m_layout = {};
    }

    // ------------------------------------------------------------
    // Internal: fetch raw shader group handles
    // ------------------------------------------------------------
    bool RtSbt::writeGroupHandles(const VulkanContext&  ctx,
                                  VkPipeline            rtPipeline,
                                  uint32_t              groupCountTotal,
                                  std::vector<uint8_t>& outHandles) const
    {
        if (!rtReady(ctx) || !rtPipeline || groupCountTotal == 0)
            return false;

        const uint32_t hSize = ctx.rtProps.shaderGroupHandleSize;
        if (hSize == 0)
            return false;

        const VkDeviceSize totalBytes = VkDeviceSize(hSize) * VkDeviceSize(groupCountTotal);
        outHandles.resize(static_cast<size_t>(totalBytes));

        const VkResult r = ctx.rtDispatch->vkGetRayTracingShaderGroupHandlesKHR(
            ctx.device,
            rtPipeline,
            0,
            groupCountTotal,
            totalBytes,
            outHandles.data());

        return r == VK_SUCCESS;
    }

    // ------------------------------------------------------------
    // Internal: submit transient command buffer and wait
    // ------------------------------------------------------------
    bool RtSbt::submitAndWait(const VulkanContext& ctx,
                              VkCommandPool        pool,
                              VkQueue              queue,
                              VkCommandBuffer      cmd) noexcept
    {
        if (!ctx.device || !pool || !queue || !cmd)
            return false;

        VkResult r = vkEndCommandBuffer(cmd);
        if (r != VK_SUCCESS)
            return false;

        VkFence           fence = VK_NULL_HANDLE;
        VkFenceCreateInfo fci   = {};
        fci.sType               = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        r = vkCreateFence(ctx.device, &fci, nullptr, &fence);
        if (r != VK_SUCCESS)
            return false;

        VkSubmitInfo si       = {};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;

        r = vkQueueSubmit(queue, 1, &si, fence);
        if (r != VK_SUCCESS)
        {
            vkDestroyFence(ctx.device, fence, nullptr);
            return false;
        }

        r = vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(ctx.device, fence, nullptr);

        // Reset pool so caller can reuse it; this is safe for transient uploads.
        vkResetCommandPool(ctx.device, pool, 0);

        return r == VK_SUCCESS;
    }

    // ------------------------------------------------------------
    // Internal: create staging + device-local sbt and upload bytes
    // ------------------------------------------------------------
    bool RtSbt::createAndUploadBytes(const VulkanContext&     ctx,
                                     std::span<const uint8_t> bytes,
                                     VkCommandPool            uploadCmdPool,
                                     VkQueue                  uploadQueue)
    {
        if (!ctx.device || !ctx.physicalDevice || bytes.empty())
            return false;

        if (!uploadCmdPool || !uploadQueue)
            return false;

        // DEVICE_LOCAL SBT (device addressable)
        m_sbt.create(ctx.device,
                     ctx.physicalDevice,
                     VkDeviceSize(bytes.size()),
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     false,
                     true);

        if (!m_sbt.valid())
            return false;

        // Staging
        GpuBuffer staging = {};
        staging.create(ctx.device,
                       ctx.physicalDevice,
                       VkDeviceSize(bytes.size()),
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       true,
                       false);

        if (!staging.valid())
        {
            m_sbt.destroy();
            return false;
        }

        staging.upload(bytes.data(), VkDeviceSize(bytes.size()), 0);

        // Record copy
        VkCommandBuffer cmd = VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo ai = {};
        ai.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool                 = uploadCmdPool;
        ai.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount          = 1;

        if (vkAllocateCommandBuffers(ctx.device, &ai, &cmd) != VK_SUCCESS)
        {
            m_sbt.destroy();
            staging.destroy();
            return false;
        }

        VkCommandBufferBeginInfo bi = {};
        bi.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(ctx.device, uploadCmdPool, 1, &cmd);
            m_sbt.destroy();
            staging.destroy();
            return false;
        }

        VkBufferCopy copy = {};
        copy.srcOffset    = 0;
        copy.dstOffset    = 0;
        copy.size         = VkDeviceSize(bytes.size());

        vkCmdCopyBuffer(cmd, staging.buffer(), m_sbt.buffer(), 1, &copy);

        // Barrier to make SBT visible to RT pipeline reads.
        VkBufferMemoryBarrier b = {};
        b.sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        b.srcAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask         = VK_ACCESS_SHADER_READ_BIT;
        b.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        b.buffer                = m_sbt.buffer();
        b.offset                = 0;
        b.size                  = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0,
                             0,
                             nullptr,
                             1,
                             &b,
                             0,
                             nullptr);

        const bool ok = submitAndWait(ctx, uploadCmdPool, uploadQueue, cmd);

        // Free cmd buffer (pool reset already, but still free the handle)
        vkFreeCommandBuffers(ctx.device, uploadCmdPool, 1, &cmd);

        // staging can be destroyed immediately
        staging.destroy();

        return ok;
    }

    // ------------------------------------------------------------
    // Public: build SBT bytes from pipeline group handles and upload
    // ------------------------------------------------------------
    bool RtSbt::buildAndUpload(const VulkanContext& ctx,
                               VkPipeline           rtPipeline,
                               uint32_t             raygenCount,
                               uint32_t             missCount,
                               uint32_t             hitCount,
                               uint32_t             callableCount,
                               VkCommandPool        uploadCmdPool,
                               VkQueue              uploadQueue)
    {
        destroy();

        if (!rtReady(ctx))
            return false;

        if (!rtPipeline)
            return false;

        const uint32_t totalGroups =
            raygenCount + missCount + hitCount + callableCount;

        if (totalGroups == 0)
            return false;

        // Compute layout (no inline data yet).
        m_layout = vkrt::compute_sbt_layout(ctx,
                                            raygenCount,
                                            missCount,
                                            hitCount,
                                            callableCount);

        if (m_layout.totalSize == 0)
            return false;

        // Fetch raw handles from driver: [group0][group1]... each = handleSize bytes.
        std::vector<uint8_t> rawHandles;
        if (!writeGroupHandles(ctx, rtPipeline, totalGroups, rawHandles))
            return false;

        const uint32_t handleSize        = ctx.rtProps.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = vkrt::handle_size_aligned(ctx);

        // Build final SBT blob with padding/stride rules.
        std::vector<uint8_t> sbtBytes;
        sbtBytes.resize(static_cast<size_t>(m_layout.totalSize), 0);

        auto writeRecord = [&](VkDeviceSize regionOffset,
                               VkDeviceSize regionStride,
                               uint32_t     recordIndexInRegion,
                               uint32_t     globalGroupIndex) {
            const VkDeviceSize dstOff =
                regionOffset + VkDeviceSize(recordIndexInRegion) * regionStride;

            const size_t dst = static_cast<size_t>(dstOff);
            const size_t src = static_cast<size_t>(globalGroupIndex) * size_t(handleSize);

            // Copy only handleSize bytes; the rest of the record stays zero (padding / inline data later).
            std::memcpy(sbtBytes.data() + dst,
                        rawHandles.data() + src,
                        size_t(handleSize));

            // Optionally you could write inline data after handleSizeAligned,
            // but we keep it empty for now.
            (void)handleSizeAligned;
        };

        // IMPORTANT: Group order must match how you created the RT pipeline groups.
        // Common convention:
        //  - raygen groups first
        //  - then miss
        //  - then hit
        //  - then callable
        uint32_t g = 0;

        for (uint32_t i = 0; i < raygenCount; ++i)
            writeRecord(m_layout.raygenOffset, m_layout.raygenStride, i, g++);

        for (uint32_t i = 0; i < missCount; ++i)
            writeRecord(m_layout.missOffset, m_layout.missStride, i, g++);

        for (uint32_t i = 0; i < hitCount; ++i)
            writeRecord(m_layout.hitOffset, m_layout.hitStride, i, g++);

        for (uint32_t i = 0; i < callableCount; ++i)
            writeRecord(m_layout.callableOffset, m_layout.callableStride, i, g++);

        // Upload to GPU SBT buffer (device local)
        if (!createAndUploadBytes(ctx, sbtBytes, uploadCmdPool, uploadQueue))
        {
            destroy();
            return false;
        }

        // Sanity: device address must be non-zero for trace rays.
        const VkDeviceAddress addr = vkrt::buffer_device_address(ctx, m_sbt.buffer());
        if (addr == 0)
        {
            destroy();
            return false;
        }

        return true;
    }

} // namespace vkrt
