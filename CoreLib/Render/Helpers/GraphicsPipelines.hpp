#pragma once

#include <vulkan/vulkan.h>

#include "VulkanContext.hpp"

/**
 * @brief Simple wrapper for a graphics pipeline handle.
 *
 * Lifetime:
 *  - Call destroy() before overwriting or at shutdown.
 *  - Does NOT own the pipeline layout (Renderer owns that).
 */
struct GraphicsPipeline
{
    VkDevice   m_device   = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    void destroy() noexcept;

    [[nodiscard]] bool valid() const noexcept
    {
        return m_pipeline != VK_NULL_HANDLE;
    }

    [[nodiscard]] VkPipeline handle() const noexcept
    {
        return m_pipeline;
    }
};

namespace vkutil
{
    /**
     * @brief Solid (unlit) mesh pipeline.
     *
     * Shaders:
     *  - SolidDraw.vert.spv
     *  - SolidDraw.frag.spv
     *
     * Matches original solidPreset.
     */
    bool createSolidPipeline(const VulkanContext&                        ctx,
                             VkRenderPass                                renderPass,
                             VkPipelineLayout                            layout,
                             VkSampleCountFlagBits                       sampleCount,
                             const VkPipelineVertexInputStateCreateInfo& vi,
                             GraphicsPipeline&                           out);

    /**
     * @brief Shaded (lit) mesh pipeline.
     *
     * Shaders:
     *  - ShadedDraw.vert.spv
     *  - ShadedDraw.frag.spv
     *
     * Matches original solidPreset (same depth/cull, different shader).
     */
    bool createShadedPipeline(const VulkanContext&                        ctx,
                              VkRenderPass                                renderPass,
                              VkPipelineLayout                            layout,
                              VkSampleCountFlagBits                       sampleCount,
                              const VkPipelineVertexInputStateCreateInfo& vi,
                              GraphicsPipeline&                           out);

    /**
     * @brief Depth-only triangle prepass.
     *
     * Shaders:
     *  - SolidDraw.vert.spv   (no fragment shader)
     *
     * Matches original depthOnlyPreset:
     *  - depth test ON
     *  - depth write ON
     *  - compare: LESS_OR_EQUAL
     *  - color writes OFF
     */
    bool createDepthOnlyPipeline(const VulkanContext&                        ctx,
                                 VkRenderPass                                renderPass,
                                 VkPipelineLayout                            layout,
                                 VkSampleCountFlagBits                       sampleCount,
                                 const VkPipelineVertexInputStateCreateInfo& vi,
                                 GraphicsPipeline&                           out);

    /**
     * @brief Wireframe pipeline for visible edges.
     *
     * Shaders:
     *  - Wireframe.vert.spv
     *  - Wireframe.frag.spv
     *
     * Matches original wirePreset:
     *  - LINE_LIST
     *  - depth test ON, write OFF
     *  - compare: LESS_OR_EQUAL
     *  - alpha blending ON (uses vertex alpha)
     */
    bool createWireframePipeline(const VulkanContext&                        ctx,
                                 VkRenderPass                                renderPass,
                                 VkPipelineLayout                            layout,
                                 VkSampleCountFlagBits                       sampleCount,
                                 const VkPipelineVertexInputStateCreateInfo& vi,
                                 GraphicsPipeline&                           out);

    /**
     * @brief Wireframe pipeline for hidden edges.
     *
     * Shaders:
     *  - Wireframe.vert.spv
     *  - Wireframe.frag.spv
     *
     * Matches original hiddenEdgePreset:
     *  - LINE_LIST
     *  - depth test ON, write OFF
     *  - compare: GREATER
     *  - alpha blending ON
     */
    bool createWireframeHiddenPipeline(const VulkanContext&                        ctx,
                                       VkRenderPass                                renderPass,
                                       VkPipelineLayout                            layout,
                                       VkSampleCountFlagBits                       sampleCount,
                                       const VkPipelineVertexInputStateCreateInfo& vi,
                                       GraphicsPipeline&                           out);

    /**
     * @brief Wireframe pipeline for overlay edges in SOLID mode.
     *
     * Shaders:
     *  - WireframeDepthBias.vert.spv
     *  - Wireframe.frag.spv
     *
     * Matches original edgeOverlayPreset (same as wirePreset but different VS).
     */
    bool createWireframeDepthBiasPipeline(const VulkanContext&                        ctx,
                                          VkRenderPass                                renderPass,
                                          VkPipelineLayout                            layout,
                                          VkSampleCountFlagBits                       sampleCount,
                                          const VkPipelineVertexInputStateCreateInfo& vi,
                                          GraphicsPipeline&                           out);

    /**
     * @brief Overlay pipeline (gizmos, tool handles, etc.).
     *
     * Shaders:
     *  - Overlay.vert.spv
     *  - Overlay.geom.spv
     *  - Overlay.frag.spv
     *
     * Matches original overlayPreset:
     *  - LINE_LIST
     *  - depth test OFF
     *  - alpha blending ON
     */
    bool createOverlayPipeline(const VulkanContext&                        ctx,
                               VkRenderPass                                renderPass,
                               VkPipelineLayout                            layout,
                               VkSampleCountFlagBits                       sampleCount,
                               const VkPipelineVertexInputStateCreateInfo& vi,
                               GraphicsPipeline&                           out);

    bool createOverlayFillPipeline(const VulkanContext&                        ctx,
                                   VkRenderPass                                renderPass,
                                   VkPipelineLayout                            layout,
                                   VkSampleCountFlagBits                       sampleCount,
                                   const VkPipelineVertexInputStateCreateInfo& vi,
                                   GraphicsPipeline&                           out);

    /**
     * @brief Triangle-based selection (ID / highlight).
     *
     * Shaders:
     *  - Selection.vert.spv
     *  - Selection.frag.spv
     *
     * Matches original selPolyPreset:
     *  - TRIANGLE_LIST
     *  - depth test ON, write OFF
     *  - compare: LESS_OR_EQUAL
     *  - alpha blending ON
     *  - depth bias enabled (dynamic state)
     */
    bool createSelectionTriPipeline(const VulkanContext&                        ctx,
                                    VkRenderPass                                renderPass,
                                    VkPipelineLayout                            layout,
                                    VkSampleCountFlagBits                       sampleCount,
                                    const VkPipelineVertexInputStateCreateInfo& vi,
                                    GraphicsPipeline&                           out);

    /**
     * @brief Vertex/point-based selection (ID / highlight).
     *
     * Shaders:
     *  - Selection.vert.spv
     *  - SelectionVert.frag.spv
     *
     * Matches original selVertPreset:
     *  - POINT_LIST
     *  - depth test ON, write OFF
     *  - compare: LESS_OR_EQUAL
     *  - alpha blending ON
     *  - depth bias enabled (dynamic state)
     */
    bool createSelectionVertPipeline(const VulkanContext&                        ctx,
                                     VkRenderPass                                renderPass,
                                     VkPipelineLayout                            layout,
                                     VkSampleCountFlagBits                       sampleCount,
                                     const VkPipelineVertexInputStateCreateInfo& vi,
                                     GraphicsPipeline&                           out);

} // namespace vkutil
