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
 *  - Swapchain resources: pipeline -> recreated on swapchain rebuild (resize), destroyed on swapchain teardown
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

    /// Destroys swapchain-level resources only (pipeline). Call on resize before swapchain rebuild.
    void destroySwapchainResources() noexcept;

    /// Destroys all resources (pipeline + buffers). Call on final shutdown.
    void destroyDeviceResources() noexcept;

    /// Creates a grid pipeline that uses an existing pipeline layout
    /// (typically Renderer::m_pipelineLayout which has the MVP UBO).
    bool createPipeline(VkRenderPass renderPass, VkPipelineLayout sharedLayout);

    /// Record draw commands for the grid into @p cmd.
    void render(VkCommandBuffer cmd);

private:
    VulkanContext* m_ctx = nullptr;

    GpuBuffer m_vertexBuffer;
    uint32_t  m_vertexCount = 0;

    VkPipeline m_pipeline = VK_NULL_HANDLE;

    void createGridData(float halfExtent = 20.f, float spacing = 0.5f);
};
