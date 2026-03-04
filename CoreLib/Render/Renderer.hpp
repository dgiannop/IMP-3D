#pragma once

#include <SysCounter.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "GpuBuffer.hpp"
#include "GpuLights.hpp"
#include "GraphicsPipelines.hpp"
#include "GridRendererVK.hpp"
#include "LightingSettings.hpp"
#include "Material.hpp"
#include "OverlayHandler.hpp"
#include "RtRenderer.hpp"
#include "VulkanContext.hpp"

class Scene;
class TextureHandler;
class Viewport;
class MeshGpuResources;
struct RenderFrameContext;

/**
 * @brief Minimal Vulkan renderer for the app (raster + shared descriptors).
 *
 * Responsibilities:
 *  - Raster pipelines and drawing.
 *  - Shared descriptor sets:
 *      * set=0 (frame globals): CameraUBO + LightsUBO (per viewport, per frame)
 *      * set=1 (materials): SSBO + texture table (per frame)
 *  - MeshGpuResources::update() scheduling outside render pass.
 *  - Delegates all ray tracing (set=2, BLAS/TLAS, trace, RT present) to RtRenderer.
 */
class Renderer
{
public:
    Renderer() noexcept;
    ~Renderer() noexcept = default;

    Renderer(const Renderer&)            = delete;
    Renderer(Renderer&&)                 = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&)      = delete;

public:
    // ============================================================
    // Lifetime / frame hooks
    // ============================================================

    [[nodiscard]] bool initDevice(const VulkanContext& ctx);
    [[nodiscard]] bool initSwapchain(VkRenderPass renderPass);
    void destroySwapchainResources() noexcept;
    void shutdown() noexcept;

    void idle(Scene* scene);

    void waitDeviceIdle() noexcept;

public:
    // ============================================================
    // Lighting settings
    // ============================================================

    void setLightingSettings(const LightingSettings& settings) noexcept;

    [[nodiscard]] const LightingSettings& lightingSettings() const noexcept;

public:
    // ============================================================
    // Rendering entry points
    // ============================================================

    void updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex);

    void renderPrePass(Viewport* vp, Scene* scene, const RenderFrameContext& fc);
    void render(Viewport* vp, Scene* scene, const RenderFrameContext& fc);

    void drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays);

public:
    // ============================================================
    // Shader-visible structs (must match GLSL/std140)
    // ============================================================

    struct CameraUBO
    {
        glm::mat4 proj     = {};
        glm::mat4 view     = {};
        glm::mat4 viewProj = {};

        glm::mat4 invProj     = {};
        glm::mat4 invView     = {};
        glm::mat4 invViewProj = {};

        glm::vec4 camPos     = {};
        glm::vec4 viewport   = {};
        glm::vec4 clearColor = {};
    };
    static_assert(sizeof(CameraUBO) % 16 == 0, "CameraUBO must be std140-aligned");

    struct PushConstants
    {
        glm::mat4 model         = {};
        glm::vec4 color         = {};
        glm::vec4 overlayParams = {};
    };
    static_assert(sizeof(PushConstants) == 96);

public:
    // ============================================================
    // Host-only structs (CPU-side bookkeeping)
    // ============================================================

    struct OverlayVertex
    {
        glm::vec3 pos       = {};
        float     thickness = 1.0f;
        glm::vec4 color     = glm::vec4(1.0f);
    };

    struct OverlayFillVertex
    {
        glm::vec3 pos   = {};
        glm::vec4 color = {};
    };
    static_assert(sizeof(OverlayFillVertex) == (3 + 4) * 4);

    struct ViewportUboState
    {
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     cameraBuffers = {};
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     lightBuffers  = {};
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> uboSets       = {}; // set=0
    };

private:
    // ============================================================
    // Raster helpers
    // ============================================================

    [[nodiscard]] bool createPipelineLayout() noexcept;

    [[nodiscard]] bool createDescriptors(uint32_t framesInFlight);
    [[nodiscard]] bool createPipelines(VkRenderPass renderPass);
    void destroyPipelines() noexcept;

    ViewportUboState& ensureViewportUboState(Viewport* vp, uint32_t frameIndex);

    void uploadMaterialsToGpu(const std::vector<Material>& materials,
                              TextureHandler&              texHandler,
                              uint32_t                     frameIndex,
                              const RenderFrameContext&    fc);

    void drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene);
    void ensureOverlayVertexCapacity(std::size_t requiredVertexCount);
    void ensureOverlayFillVertexCapacity(std::size_t requiredVertexCount);
    void drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene);

    // Shared helper to update set=0 buffers for a viewport+frame.
    void updateViewportFrameGlobals(Viewport* vp, Scene* scene, uint32_t frameIndex) noexcept;

private:
    // ============================================================
    // Context / frame config
    // ============================================================

    VulkanContext m_ctx            = {};
    uint32_t      m_framesInFlight = 2;

    static constexpr uint32_t kMaxViewports = 8;

private:
    // ============================================================
    // Raster pipelines / layout
    // ============================================================

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    GraphicsPipeline m_solidPipeline       = {};
    GraphicsPipeline m_shadedPipeline      = {};
    GraphicsPipeline m_depthOnlyPipeline   = {};
    GraphicsPipeline m_wirePipeline        = {};
    GraphicsPipeline m_wireHiddenPipeline  = {};
    GraphicsPipeline m_wireOverlayPipeline = {};

    GraphicsPipeline m_overlayPipeline     = {};
    GraphicsPipeline m_overlayFillPipeline = {};

    GraphicsPipeline m_selVertPipeline       = {};
    GraphicsPipeline m_selEdgePipeline       = {};
    GraphicsPipeline m_selPolyPipeline       = {};
    GraphicsPipeline m_selVertHiddenPipeline = {};
    GraphicsPipeline m_selEdgeHiddenPipeline = {};
    GraphicsPipeline m_selPolyHiddenPipeline = {};

    std::unique_ptr<GridRendererVK> m_grid;

private:
    // ============================================================
    // Shared descriptors / frame globals
    // ============================================================

    DescriptorSetLayout m_descriptorSetLayout = {}; // set=0
    DescriptorSetLayout m_materialSetLayout   = {}; // set=1
    DescriptorPool      m_descriptorPool      = {};

    std::unordered_map<Viewport*, ViewportUboState>      m_viewportUbos = {};
    std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> m_materialSets = {};

    HeadlightSettings m_headlight        = {};
    LightingSettings  m_lightingSettings = {};

private:
    // ============================================================
    // Materials SSBO (per-frame)
    // ============================================================

    std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     m_materialBuffers         = {};
    std::array<std::uint64_t, vkcfg::kMaxFramesInFlight> m_materialCounterPerFrame = {};

private:
    // ============================================================
    // Overlay vertex buffers
    // ============================================================

    GpuBuffer   m_overlayVertexBuffer   = {};
    std::size_t m_overlayVertexCapacity = 0;

    GpuBuffer   m_overlayFillVertexBuffer   = {};
    std::size_t m_overlayFillVertexCapacity = 0;

private:
    // ============================================================
    // Ray tracing module (RT-only)
    // ============================================================

    RtRenderer m_rt = {};

private:
    // ============================================================
    // Misc helpers
    // ============================================================

    template<typename Fn>
    void forEachVisibleMesh(Scene* scene, Fn&& fn);
};
