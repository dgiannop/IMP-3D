#pragma once

#include <vulkan/vulkan.h>

#include "VulkanContext.hpp"

/**
 * @file VulkanImage.hpp
 * @brief RAII wrapper for a simple 2-D device-local Vulkan image + view.
 *
 * Covers the common render-target / storage-image pattern used by
 * RtRenderer (radiance, normal, depth, albedo AOV buffers) and
 * RtDenoiser (ping, pong, output filter buffers):
 *
 *  - Single mip-level, single array layer, VK_SAMPLE_COUNT_1_BIT.
 *  - Device-local memory only (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).
 *  - Automatically creates a matching VkImageView with the correct aspect
 *    mask derived from the format (colour, depth, or depth+stencil).
 *  - Tracks the current VkImageLayout so barrier helpers can derive the
 *    correct source stage / access masks without caller book-keeping.
 *  - ensure() re-uses the existing allocation when dimensions and format
 *    are unchanged, and defers destruction of the old resources via
 *    DeferredDeletion when a resize is required.
 *
 * @note This class is intentionally minimal. It does not handle mip chains,
 *       array textures, or multi-sample images. Add a specialised subclass
 *       or factory if those are needed.
 */

/**
 * @class VulkanImage
 * @brief RAII owner of a VkImage + VkDeviceMemory + VkImageView triplet.
 *
 * Typical frame loop usage (colour storage image):
 * @code
 *   // Once per resize (or first use):
 *   image.ensure(device, physDev, w, h,
 *                VK_FORMAT_R16G16B16A16_SFLOAT,
 *                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
 *                fc);
 *
 *   // Before writing (ray-tracing / compute):
 *   image.transitionToGeneral(cmd);
 *   // ... vkCmdTraceRaysKHR / vkCmdDispatch ...
 *
 *   // Before sampling (fragment / compute read):
 *   image.transitionToShaderRead(cmd);
 * @endcode
 */
class VulkanImage
{
public:
    VulkanImage() noexcept = default;
    ~VulkanImage() noexcept { destroy(); }

    VulkanImage(const VulkanImage&)            = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

    VulkanImage(VulkanImage&& other) noexcept;
    VulkanImage& operator=(VulkanImage&& other) noexcept;

public:
    // =========================================================
    // Lifetime
    // =========================================================

    /**
     * @brief Allocates a new device-local 2-D image and a matching view.
     *
     * The aspect mask is derived automatically from @p format:
     *  - Depth-only formats      → VK_IMAGE_ASPECT_DEPTH_BIT
     *  - Depth+stencil formats   → DEPTH | STENCIL
     *  - Everything else         → VK_IMAGE_ASPECT_COLOR_BIT
     *
     * The image is left in VK_IMAGE_LAYOUT_UNDEFINED; call
     * transitionToGeneral() before the first write.
     *
     * @pre  valid() == false  (do not call on a live image; use ensure()).
     * @pre  width > 0 && height > 0 && format != VK_FORMAT_UNDEFINED.
     *
     * @param device         Logical device.
     * @param physicalDevice Physical device used for memory-type queries.
     * @param width          Image width in pixels.
     * @param height         Image height in pixels.
     * @param format         Desired VkFormat.
     * @param usage          VkImageUsageFlags (e.g. STORAGE | SAMPLED).
     *
     * @return true on success. On failure the object remains in the empty
     *         state and an error is printed to stderr.
     */
    [[nodiscard]] bool create(VkDevice          device,
                              VkPhysicalDevice  physicalDevice,
                              uint32_t          width,
                              uint32_t          height,
                              VkFormat          format,
                              VkImageUsageFlags usage);

    /**
     * @brief Ensures the image matches the requested dimensions and format.
     *
     * - If the image is already live with matching width, height and format,
     *   this is a no-op and returns true immediately.
     * - Otherwise the current resources are released (deferred if @p deferred
     *   is non-null, immediate otherwise) and a new image is created via
     *   create().
     *
     * @param device         Logical device.
     * @param physicalDevice Physical device.
     * @param width          Required width in pixels.
     * @param height         Required height in pixels.
     * @param format         Required VkFormat.
     * @param usage          VkImageUsageFlags for the new image.
     * @param deferred       Optional deferred-deletion queue. When non-null
     *                       the old VkImage/VkDeviceMemory/VkImageView are
     *                       enqueued for destruction on @p frameIndex rather
     *                       than destroyed immediately.
     * @param frameIndex     Frame slot passed to DeferredDeletion::enqueue().
     *                       Ignored when @p deferred is null.
     *
     * @return true on success.
     */
    [[nodiscard]] bool ensure(VkDevice          device,
                              VkPhysicalDevice  physicalDevice,
                              uint32_t          width,
                              uint32_t          height,
                              VkFormat          format,
                              VkImageUsageFlags usage,
                              DeferredDeletion* deferred   = nullptr,
                              uint32_t          frameIndex = 0);

    /**
     * @brief Convenience overload that unpacks deferred and frameIndex
     *        from a RenderFrameContext.
     *
     * Equivalent to:
     * @code
     *   ensure(device, physicalDevice, w, h, format, usage,
     *          fc.deferred, fc.frameIndex);
     * @endcode
     */
    [[nodiscard]] bool ensure(VkDevice                  device,
                              VkPhysicalDevice          physicalDevice,
                              uint32_t                  width,
                              uint32_t                  height,
                              VkFormat                  format,
                              VkImageUsageFlags         usage,
                              const RenderFrameContext& fc);

    /**
     * @brief Destroys the image, view and memory immediately.
     *
     * Safe to call on an already-empty object (no-op).
     * After this call valid() returns false.
     */
    void destroy() noexcept;

public:
    // =========================================================
    // Layout transitions
    // =========================================================

    /**
     * @brief Transitions the image to VK_IMAGE_LAYOUT_GENERAL.
     *
     * Selects the appropriate source stage and access masks based on the
     * internally tracked current layout:
     *
     * | Current layout                    | src stage                    | src access          |
     * |-----------------------------------|------------------------------|---------------------|
     * | UNDEFINED                         | TOP_OF_PIPE                  | 0                   |
     * | SHADER_READ_ONLY_OPTIMAL          | FRAGMENT + COMPUTE           | SHADER_READ         |
     * | DEPTH_STENCIL_READ_ONLY_OPTIMAL   | EARLY + LATE FRAGMENT TESTS  | DEPTH_STENCIL_READ  |
     * | any other                         | ALL_COMMANDS (safe fallback) | MEMORY_READ + WRITE |
     *
     * @param cmd      Recording command buffer. Must be outside a render pass.
     * @param dstStage Destination pipeline stage(s) that will write the image.
     *                 Defaults to TRANSFER | RAY_TRACING_SHADER, which is
     *                 correct for RtRenderer AOV buffers. Pass
     *                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT for denoiser images.
     *
     * @note Clears the needsInit() flag as a side-effect.
     */
    void transitionToGeneral(VkCommandBuffer      cmd,
                             VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT |
                                                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR) noexcept;

    /**
     * @brief Transitions the image from GENERAL to
     *        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
     *
     * Expected call sequence:
     * @code
     *   transitionToGeneral(cmd);     // before write
     *   vkCmdDispatch / TraceRays
     *   transitionToShaderRead(cmd);  // before sample
     * @endcode
     *
     * @pre  layout() == VK_IMAGE_LAYOUT_GENERAL.
     *       Asserts in debug builds if this precondition is violated.
     *
     * @param cmd      Recording command buffer. Must be outside a render pass.
     * @param srcStage Pipeline stage(s) that last wrote the image.
     *                 Defaults to RAY_TRACING_SHADER, correct for RtRenderer.
     *                 Pass VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT for denoiser.
     * @param dstStage Pipeline stage(s) that will next read the image.
     *                 Defaults to COMPUTE | FRAGMENT.
     */
    void transitionToShaderRead(VkCommandBuffer      cmd,
                                VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) noexcept;

public:
    // =========================================================
    // Accessors
    // =========================================================

    /// @brief Underlying VkImage handle, or VK_NULL_HANDLE if empty.
    [[nodiscard]] VkImage image() const noexcept { return m_image; }

    /// @brief Image view, or VK_NULL_HANDLE if empty.
    [[nodiscard]] VkImageView view() const noexcept { return m_view; }

    /// @brief Image width in pixels, or 0 if empty.
    [[nodiscard]] uint32_t width() const noexcept { return m_width; }

    /// @brief Image height in pixels, or 0 if empty.
    [[nodiscard]] uint32_t height() const noexcept { return m_height; }

    /// @brief VkFormat of the image, or VK_FORMAT_UNDEFINED if empty.
    [[nodiscard]] VkFormat format() const noexcept { return m_format; }

    /// @brief Aspect mask derived from the format at creation time.
    [[nodiscard]] VkImageAspectFlags aspectMask() const noexcept { return m_aspectMask; }

    /// @brief Currently tracked VkImageLayout.
    [[nodiscard]] VkImageLayout layout() const noexcept { return m_layout; }

    /// @brief Returns true when the image and view are live.
    [[nodiscard]] bool valid() const noexcept { return m_image != VK_NULL_HANDLE; }

    /**
     * @brief Returns true if the image has never been transitioned away
     *        from VK_IMAGE_LAYOUT_UNDEFINED.
     *
     * Callers can use this flag to decide whether to issue a clear before
     * the first write (e.g. to avoid reading stale data from a resized
     * image). The flag is cleared automatically by transitionToGeneral().
     */
    [[nodiscard]] bool needsInit() const noexcept { return m_needsInit; }

    /// @brief Manually clears the needsInit flag if the caller handles
    ///        initialisation itself.
    void clearNeedsInit() noexcept { m_needsInit = false; }

private:
    // =========================================================
    // Helpers
    // =========================================================

    /**
     * @brief Derives the correct VkImageAspectFlags for a given format.
     *
     * Used internally by create() to set m_aspectMask. Exposed as a static
     * utility in case callers need the same logic (e.g. when creating
     * barriers manually for images not owned by VulkanImage).
     *
     * | Format group          | Result                              |
     * |-----------------------|-------------------------------------|
     * | D32, D16              | DEPTH                               |
     * | D24S8, D32S8, D16S8   | DEPTH + STENCIL                     |
     * | all others            | COLOR                               |
     */
    [[nodiscard]] static VkImageAspectFlags aspectMaskForFormat(VkFormat format) noexcept;

private:
    VkDevice           m_device     = VK_NULL_HANDLE;
    VkImage            m_image      = VK_NULL_HANDLE;
    VkDeviceMemory     m_memory     = VK_NULL_HANDLE;
    VkImageView        m_view       = VK_NULL_HANDLE;
    uint32_t           m_width      = 0;
    uint32_t           m_height     = 0;
    VkFormat           m_format     = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout      m_layout     = VK_IMAGE_LAYOUT_UNDEFINED;
    bool               m_needsInit  = true;
};
