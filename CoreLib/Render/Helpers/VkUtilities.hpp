//============================================================
// VkUtilities.hpp
//============================================================
#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

namespace vkutil
{
    // ============================================================================
    // Common helpers
    // ============================================================================

    VkTransformMatrixKHR toVkTransformMatrix(const glm::mat4& m) noexcept;

    VkClearColorValue toVkClearColor(const glm::vec4& color) noexcept;

    void printVkResult(VkResult r, const char* where);

    // ============================================================================
    // Upload helpers (frame-cmd path)
    // ============================================================================

    /**
     * @brief Ensure a HOST_VISIBLE upload/staging buffer has at least @p bytes capacity.
     *
     * The buffer is created with:
     * - VK_BUFFER_USAGE_TRANSFER_SRC_BIT
     * - HOST_VISIBLE | HOST_COHERENT
     * - persistent map enabled
     */
    bool ensureUploadBuffer(const VulkanContext& ctx,
                            GpuBuffer&           upload,
                            VkDeviceSize         bytes) noexcept;

    /**
     * @brief Record CPU->staging->device-local copy into an EXISTING command buffer.
     *
     * IMPORTANT:
     * - This records vkCmdCopyBuffer only.
     * - It does NOT insert any barriers.
     * - MUST be called OUTSIDE a render pass.
     *
     * @param upload Reusable HOST_VISIBLE staging buffer (persistently mapped).
     */
    bool recordUploadToDeviceLocalBuffer(const VulkanContext& ctx,
                                         VkCommandBuffer      cmd,
                                         VkBuffer             dst,
                                         VkDeviceSize         dstOffset,
                                         const void*          data,
                                         VkDeviceSize         bytes) noexcept;

    /**
     * @brief Create a device-local buffer (capacity only, no upload).
     *
     * NOTE: Adds VK_BUFFER_USAGE_TRANSFER_DST_BIT automatically (since you will upload into it).
     */
    [[nodiscard]] GpuBuffer createDeviceLocalBufferEmpty(const VulkanContext& ctx,
                                                         VkDeviceSize         capacity,
                                                         VkBufferUsageFlags   usage,
                                                         bool                 deviceAddress);

    inline void createDeviceLocalBufferEmpty(const VulkanContext& ctx,
                                             VkDeviceSize         capacity,
                                             VkBufferUsageFlags   usage,
                                             bool                 deviceAddress,
                                             GpuBuffer&           out)
    {
        out = createDeviceLocalBufferEmpty(ctx, capacity, usage, deviceAddress);
    }

    // ============================================================================
    // Barriers (use after recordUploadToDeviceLocalBuffer as needed)
    // ============================================================================

    /// Transfer-write -> vertex attribute read (vertex buffers)
    void barrierTransferToVertexAttributeRead(VkCommandBuffer cmd);

    /// Transfer-write -> index read (index buffers)
    void barrierTransferToIndexRead(VkCommandBuffer cmd);

    /// Transfer-write -> graphics shader read (UBO/SSBO read in VS/FS)
    void barrierTransferToGraphicsShaderRead(VkCommandBuffer cmd);

    /// Transfer-write -> ray tracing shader read (SSBO read in RT pipeline)
    void barrierTransferToRtShaderRead(VkCommandBuffer cmd);

    /**
     * @brief Transfer-write -> acceleration structure build read (AS build inputs).
     *
     * Use this after uploading any buffer that will be consumed by vkCmdBuildAccelerationStructuresKHR
     * or vkCmdBuildAccelerationStructuresIndirectKHR via:
     * VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
     *
     * Example buffers:
     * - vertex/index buffers used for BLAS build input
     * - instance buffers used for TLAS build input
     */
    void barrierTransferToAsBuildRead(VkCommandBuffer cmd);

    /// AS build writes -> RT shader reads (TLAS/BLAS visibility for trace rays)
    void barrierAsBuildToTrace(VkCommandBuffer cmd);

    // ============================================================================
    // Device address helpers
    // ============================================================================

    VkDeviceAddress bufferDeviceAddress(VkDevice device, VkBuffer buf) noexcept;

    VkDeviceAddress bufferDeviceAddress(VkDevice device, const GpuBuffer& buf) noexcept;

    uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) noexcept;

    // ============================================================================
    // Common fixed-function state helpers
    // ============================================================================

    VkPipelineInputAssemblyStateCreateInfo makeInputAssembly(VkPrimitiveTopology topology);

    VkPipelineViewportStateCreateInfo makeViewportState();

    VkPipelineRasterizationStateCreateInfo makeRasterState(VkCullModeFlags cullMode,
                                                           VkPolygonMode   mode      = VK_POLYGON_MODE_FILL,
                                                           VkFrontFace     frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                           float           lineWidth = 1.0f);

    VkPipelineMultisampleStateCreateInfo makeMultisampleState(VkSampleCountFlagBits samples);

    VkPipelineDepthStencilStateCreateInfo makeDepthStencilState(bool depthWriteEnable);

    VkPipelineColorBlendAttachmentState makeColorBlendAttachment(bool enableBlend = true);

    VkPipelineColorBlendStateCreateInfo makeColorBlendState(const VkPipelineColorBlendAttachmentState* attachment);

    VkPipelineDynamicStateCreateInfo makeDynamicState(const VkDynamicState* states, uint32_t count);

    void setViewportAndScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height);

    // ============================================================================
    // One-time Command Buffer Utilities (keep for non-frame work if you still use them)
    // ============================================================================

    struct OneTimeCmd
    {
        VkDevice        device      = VK_NULL_HANDLE;
        VkCommandPool   pool        = VK_NULL_HANDLE;
        VkCommandBuffer cmd         = VK_NULL_HANDLE;
        VkQueue         queue       = VK_NULL_HANDLE;
        uint32_t        queueFamily = 0;
    };

    OneTimeCmd beginTransientCmd(const VulkanContext& ctx);

    bool submitTransientCmd(const OneTimeCmd& otc) noexcept;

    bool TransientCmd(const VulkanContext& ctx, const std::function<void(VkCommandBuffer)>& record);

    // ============================================================================
    // Device-Local Buffer Upload Helpers (legacy transient path - keep for now)
    // ============================================================================

    GpuBuffer createDeviceLocalBuffer(const VulkanContext& ctx,
                                      VkDeviceSize         capacity,
                                      VkBufferUsageFlags   usage,
                                      const void*          data,
                                      VkDeviceSize         copySize,
                                      bool                 deviceAddress = false);

    void updateDeviceLocalBuffer(const VulkanContext& ctx,
                                 const GpuBuffer&     dst,
                                 VkDeviceSize         dstOffset,
                                 VkDeviceSize         size,
                                 const void*          data);

    // ============================================================================
    // Pipeline helper
    // ============================================================================

    struct GraphicsPipelineDesc
    {
        VkRenderPass     renderPass = VK_NULL_HANDLE;
        uint32_t         subpass    = 0;
        VkPipelineLayout layout     = VK_NULL_HANDLE;

        const VkPipelineShaderStageCreateInfo*        stages        = nullptr;
        uint32_t                                      stageCount    = 0;
        const VkPipelineVertexInputStateCreateInfo*   vertexInput   = nullptr;
        const VkPipelineInputAssemblyStateCreateInfo* inputAssembly = nullptr;
        const VkPipelineViewportStateCreateInfo*      viewport      = nullptr;
        const VkPipelineRasterizationStateCreateInfo* rasterization = nullptr;
        const VkPipelineMultisampleStateCreateInfo*   multisample   = nullptr;
        const VkPipelineDepthStencilStateCreateInfo*  depthStencil  = nullptr;
        const VkPipelineColorBlendStateCreateInfo*    colorBlend    = nullptr;
        const VkPipelineDynamicStateCreateInfo*       dynamicState  = nullptr;
    };

    VkPipeline createGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc);

} // namespace vkutil

namespace vkutil
{
    struct FrameUploadTrash
    {
        std::vector<GpuBuffer> staging;
    };

    void              setFrameUploadTrash(FrameUploadTrash* trash) noexcept;
    FrameUploadTrash* frameUploadTrash() noexcept;
} // namespace vkutil
