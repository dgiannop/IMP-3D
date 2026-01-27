#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

/**
 * @brief Draws the world-space scene grid (floor grid) for a Viewport.
 *
 * Lifetime split:
 *  - Device resources: vertex buffer (grid geometry) -> created once, destroyed on shutdown
 *  - Swapchain resources: pipelines -> recreated on swapchain rebuild (resize), destroyed on swapchain teardown
 *
 * There are two pipelines:
 *  - depth-tested   : occluded by geometry (solid/shaded)
 *  - xray (no depth): shows through geometry (wireframe)
 */
class GridRendererVK
{
public:
    explicit GridRendererVK(VulkanContext* ctx);
    ~GridRendererVK() noexcept;

    GridRendererVK(const GridRendererVK&)            = delete;
    GridRendererVK& operator=(const GridRendererVK&) = delete;

    /// Creates device-level resources (grid vertex buffer). Call once after device init.
    void createDeviceResources();

    /// Destroys swapchain-level resources only (pipelines). Call on resize before swapchain rebuild.
    void destroySwapchainResources() noexcept;

    /// Destroys all resources (pipelines + buffers). Call on final shutdown.
    void destroyDeviceResources() noexcept;

    /// Creates both grid pipelines (depth-tested and xray) that use an existing pipeline layout
    /// (typically Renderer::m_pipelineLayout which has the MVP+Lights UBO).
    bool createPipelines(VkRenderPass renderPass, VkPipelineLayout sharedLayout);

    /// Record draw commands for the grid into @p cmd.
    ///
    /// @param xray If true, uses the no-depth-test pipeline (shows grid through geometry),
    ///             otherwise uses the depth-tested pipeline (grid occluded by meshes).
    void render(VkCommandBuffer cmd, bool xray);

private:
    struct GridVert
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    VulkanContext* m_ctx = nullptr;

    GpuBuffer m_vertexBuffer;
    uint32_t  m_vertexCount = 0;

    // Two pipelines: depth-tested and xray
    VkPipeline m_pipelineDepthTested = VK_NULL_HANDLE;
    VkPipeline m_pipelineXray        = VK_NULL_HANDLE;

    void createGridData(float halfExtent = 40.f, float spacing = 0.5f);
};
