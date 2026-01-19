#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "VulkanContext.hpp"

struct VulkanContext;
class GpuBuffer;

namespace vkutil
{
    // ============================================================================
    // Common helpers
    // ============================================================================

    VkTransformMatrixKHR toVkTransformMatrix(const glm::mat4& m) noexcept;

    VkClearColorValue toVkClearColor(const glm::vec4& color) noexcept;

    void printVkResult(VkResult r, const char* where);

    // ============================================================================
    // Common fixed-function state helpers
    // ============================================================================

    VkDeviceAddress bufferDeviceAddress(VkDevice device, VkBuffer buf) noexcept;

    // Overload if your GpuBuffer type exists:
    VkDeviceAddress bufferDeviceAddress(VkDevice device, const GpuBuffer& buf) noexcept;

    // VkDeviceAddress bufferDeviceAddress(const VulkanContext& ctx, VkBuffer buf);

    uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) noexcept;

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
    // One-time Command Buffer Utilities
    // ============================================================================

    /**
     * @brief Lightweight bundle storing resources required for a one-shot command recording.
     *
     * A `OneTimeCmd` struct holds:
     * - command pool (transient, auto-destroyed)
     * - command buffer (allocated from the pool)
     * - queue + family index used to submit work
     *
     * This is used internally by `beginOneTimeCommands()` / `endOneTimeCommands()` or via
     * the convenience `submitOneTimeCommands()` wrapper.
     */
    struct OneTimeCmd
    {
        VkDevice        device      = VK_NULL_HANDLE; ///< Vulkan device used for allocation.
        VkCommandPool   pool        = VK_NULL_HANDLE; ///< Transient pool for this command buffer.
        VkCommandBuffer cmd         = VK_NULL_HANDLE; ///< Primary command buffer.
        VkQueue         queue       = VK_NULL_HANDLE; ///< Queue used for submission.
        uint32_t        queueFamily = 0;              ///< Queue family index.
    };

    /**
     * @brief Allocate & begin a transient, one-time submit command buffer.
     *
     * Creates a command pool with `VK_COMMAND_POOL_CREATE_TRANSIENT_BIT`,
     * allocates a primary command buffer, and begins recording with
     * `VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT`.
     *
     * @param ctx Vulkan device + queue context.
     * @return A valid `OneTimeCmd` on success; invalid handles if allocation fails.
     *
     * @warning Must be finished with `submitTransientCmd()` to submit & clean up.
     */
    OneTimeCmd beginTransientCmd(const VulkanContext& ctx);

    /**
     * @brief End recording, submit to queue, wait, then destroy the pool/cmd.
     *
     * This finalizes the command buffer, submits it once, waits for fence,
     * frees the command buffer and destroys the command pool.
     *
     * @param otc The handle returned by `beginTransientCmd()`.
     */
    bool submitTransientCmd(const OneTimeCmd& otc) noexcept;

    /**
     * @brief One-call wrapper: begin → record(cmd) → end.
     *
     * Example:
     * @code
     * TransientCmd(ctx, [&](VkCommandBuffer cmd) {
     *     vkCmdCopyBuffer(cmd, src, dst, 1, &region);
     * });
     * @endcode
     *
     * @param ctx Vulkan context for allocation & submission.
     * @param record Lambda/function that records commands into the buffer.
     * @return true if successfully submitted, false if allocation or recording failed.
     */
    bool TransientCmd(const VulkanContext& ctx, const std::function<void(VkCommandBuffer)>& record);

    // ============================================================================
    // Device-Local Buffer Upload Helpers (staged copy)
    // ============================================================================

    /**
     * @brief Create a device-local GPU buffer with extra capacity, uploading only @p copySize bytes.
     *
     * This form supports buffers whose allocated size (`capacity`) is larger than the initial
     * data payload (`copySize`). Used for growing vertex/index buffers without recreating on
     * every small edit.
     *
     * Internally performs:
     * 1. Create device-local buffer (`capacity` bytes)
     * 2. Create staging buffer (`copySize` bytes)
     * 3. Upload CPU → staging
     * 4. Copy staging → device (one-time command)
     *
     * If @p deviceAddress is true, the buffer is created with device-address support:
     * - Adds `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` to the final usage flags
     * - Allocates memory with `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`
     *
     * This is required for features that read buffer contents via device addresses
     * (e.g., ray tracing acceleration structure build inputs).
     *
     * @param ctx           Vulkan device context (device, phys, queue).
     * @param capacity      Total bytes allocated in the GPU buffer.
     * @param usage         Buffer usage flags (VK_BUFFER_USAGE_*). `VK_BUFFER_USAGE_TRANSFER_DST_BIT`
     *                      is added automatically.
     * @param data          Pointer to CPU source data (must contain at least @p copySize bytes).
     * @param copySize      Number of bytes to upload to the start of the buffer.
     * @param deviceAddress If true, enable shader device address for this buffer.
     * @return GpuBuffer handle. `valid()==false` if creation/upload failed.
     */
    GpuBuffer createDeviceLocalBuffer(const VulkanContext& ctx,
                                      VkDeviceSize         capacity,
                                      VkBufferUsageFlags   usage,
                                      const void*          data,
                                      VkDeviceSize         copySize,
                                      bool                 deviceAddress = false);

    /**
     * @brief Create & upload a buffer with no reserved extra capacity.
     *
     * Convenience version of the above — allocates exactly `size` bytes and uploads
     * the full data range.
     *
     * @param ctx   Vulkan device context.
     * @param size  Bytes to allocate & upload.
     * @param usage Buffer usage flags (DST bit added automatically).
     * @param data  Pointer to CPU data of length @p size.
     * @return GPU buffer, invalid on failure.
     */
    GpuBuffer createDeviceLocalBuffer(const VulkanContext& ctx,
                                      VkDeviceSize         size,
                                      VkBufferUsageFlags   usage,
                                      const void*          data);

    /**
     * @brief Update part of a device-local buffer via a transient staging buffer.
     *
     * This copies @p size bytes from CPU → staging → @p dst at offset @p dstOffset.
     * The buffer must have been created with `VK_BUFFER_USAGE_TRANSFER_DST_BIT`.
     *
     * @param ctx       Vulkan context providing queue access.
     * @param dst       The device-local buffer to update.
     * @param dstOffset Destination byte offset inside @p dst.
     * @param size      Number of bytes to copy.
     * @param data      CPU source pointer (must be at least size).
     */
    void updateDeviceLocalBuffer(const VulkanContext& ctx,
                                 const GpuBuffer&     dst,
                                 VkDeviceSize         dstOffset,
                                 VkDeviceSize         size,
                                 const void*          data);

    /// Lightweight descriptor for a single graphics pipeline.
    /// All pointers are non-owning; they must live at least until
    /// vkCreateGraphicsPipelines returns.
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

    /// Create a graphics pipeline from the descriptor.
    /// Returns VK_NULL_HANDLE on failure.
    VkPipeline createGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc);

} // namespace vkutil
