//============================================================
// Renderer.hpp  (FULL REPLACEMENT — CameraUBO unified)
//============================================================
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
#include <unordered_set>
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
#include "RenderGeometry.hpp" // for render::geom::...
#include "RtPipeline.hpp"
#include "RtPresentPipeline.hpp"
#include "RtSbt.hpp"
#include "VulkanContext.hpp"

class Scene;
class SceneMesh;
class TextureHandler;
class Viewport;
class MeshGpuResources;

/**
 * @brief Minimal Vulkan renderer for the app.
 *
 * Lifetime overview:
 *
 *  - Device lifetime (initDevice() → shutdown()):
 *      * Descriptor set layouts / pools.
 *      * Pipeline layouts (raster + RT).
 *      * Material SSBO buffer.
 *      * Overlay vertex buffers.
 *      * RT pipeline, SBT, RT descriptor layout/pool, RT sampler, RT upload pool.
 *      * GridRendererVK device resources.
 *
 *  - Swapchain lifetime (initSwapchain() → destroySwapchainResources()):
 *      * Graphics pipelines (raster, selection, overlays).
 *      * RtPresentPipeline (fullscreen present) and grid swapchain pipelines.
 *
 *  - Per-frame (global, not tied to a specific viewport):
 *      * Material descriptor sets (set = 1).
 *      * TLAS frames (one TLAS per frame-in-flight).
 *
 *  - Per-viewport:
 *      * m_viewportUbos: frame-global UBOs (set = 0) for each viewport.
 *      * m_rtViewports: RT images, RT per-frame sets/buffers, RT scratch for
 *        each viewport.
 *
 *  - Per-viewport + frame:
 *      * CameraUBO + lights UBO.
 *      * RT instance SSBO, RT storage image, RT scratch.
 *
 * Notes on frames-in-flight:
 *  - Storage is fixed-size (std::array) for vkcfg::kMaxFramesInFlight.
 *  - m_framesInFlight is the active runtime count (clamped to [1..kMaxFramesInFlight]).
 *  - Only indices [0..m_framesInFlight-1] are used.
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
    // Public API: Lifetime / frame hooks
    // ============================================================

    /// Device lifetime: call once after VkDevice is ready.
    bool initDevice(const VulkanContext& ctx);

    /// Swapchain lifetime: call whenever swapchain/render pass changes.
    bool initSwapchain(VkRenderPass renderPass);

    /// Swapchain teardown: call on resize / swapchain destruction.
    void destroySwapchainResources() noexcept;

    /// Device teardown: destroys all device-level and swapchain-level resources.
    void shutdown() noexcept;

    /// Called when the app is idle; used for RT TLAS change tracking hookup.
    void idle(Scene* scene);

    /// Explicit vkDeviceWaitIdle wrapper.
    void waitDeviceIdle() noexcept;

    // ------------------------------------------------------------
    // Lighting settings
    // ------------------------------------------------------------

    /**
     * @brief Apply lighting settings (render policy).
     *
     * Called by Scene when UI changes lighting controls.
     */
    void setLightingSettings(const LightingSettings& settings) noexcept;

    /**
     * @brief Retrieve current lighting settings.
     */
    [[nodiscard]] const LightingSettings& lightingSettings() const noexcept;

public:
    // ============================================================
    // Public API: Rendering entry points
    // ============================================================

    /// Update per-frame material texture descriptor table (set = 1).
    void updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex);

    /**
     * @brief Pre-pass work that must occur OUTSIDE a render pass.
     *
     * - MeshGpuResources::update()
     * - RT dispatch (vkCmdTraceRaysKHR) for DrawMode::RAY_TRACE
     */
    void renderPrePass(Viewport* vp, Scene* scene, const RenderFrameContext& fc);

    /**
     * @brief Main pass: raster rendering OR RT present + overlays/selection.
     *
     * Called per-viewport, per-frame, inside the active render pass.
     */
    void render(Viewport* vp, Scene* scene, const RenderFrameContext& fc);

    /// Overlay draw helpers (called inside raster pass as needed).
    void drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays);

public:
    // ============================================================
    // Shader-visible structs (must match GLSL/std140/std430)
    // ============================================================

    /**
     * @brief Unified camera UBO shared by raster + RT (set=0, binding=0).
     *
     * Conventions:
     *  - proj/view matrices are whatever Viewport produces (Vulkan RH + ZO + Y-flip).
     *  - viewProj = proj * view
     *  - inv* are true inverses of the above.
     *
     * viewport = (width, height, 1/width, 1/height) in pixels.
     */
    struct CameraUBO
    {
        glm::mat4 proj     = {}; // VIEW  -> CLIP
        glm::mat4 view     = {}; // WORLD -> VIEW
        glm::mat4 viewProj = {}; // WORLD -> CLIP

        glm::mat4 invProj     = {}; // CLIP  -> VIEW
        glm::mat4 invView     = {}; // VIEW  -> WORLD
        glm::mat4 invViewProj = {}; // CLIP  -> WORLD

        glm::vec4 camPos     = {}; // world-space camera position (xyz, 1)
        glm::vec4 viewport   = {}; // (w, h, 1/w, 1/h)
        glm::vec4 clearColor = {}; // RT clear color
    };
    static_assert(sizeof(CameraUBO) % 16 == 0, "CameraUBO must be std140-aligned");

    // Push constants used by raster + overlays (keep layout stable with shader)
    struct PushConstants
    {
        glm::mat4 model         = {};
        glm::vec4 color         = {};
        glm::vec4 overlayParams = {};
    };
    static_assert(sizeof(PushConstants) == 96);

    // set=2 binding=3 (RT path) - std430 SSBO element, per-instance
    struct alignas(8) RtInstanceData
    {
        uint64_t posAdr   = 0;
        uint64_t idxAdr   = 0;
        uint64_t nrmAdr   = 0;
        uint64_t uvAdr    = 0;
        uint64_t matIdAdr = 0;

        uint32_t triCount = 0;
        uint32_t _pad0    = 0;
        uint32_t _pad1    = 0;
        uint32_t _pad2    = 0;
    };
    static_assert(sizeof(RtInstanceData) == 56);
    static_assert(alignof(RtInstanceData) == 8);

public:
    // ============================================================
    // Host-only structs (CPU-side bookkeeping)
    // ============================================================

    // CPU-side vertex format for wide-line overlays (gizmos, handles, etc.)
    struct OverlayVertex
    {
        glm::vec3 pos       = {};
        float     thickness = 1.0f;
        glm::vec4 color     = glm::vec4(1.0f);
    };

    // CPU-side vertex format for filled overlay polygons (triangles).
    struct OverlayFillVertex
    {
        glm::vec3 pos   = {};
        glm::vec4 color = {};
    };
    static_assert(sizeof(OverlayFillVertex) == (3 + 4) * 4);

    /**
     * @brief Per-viewport frame-global UBO state (set = 0).
     *
     * Scope:
     *  - One instance per Viewport* (Renderer::m_viewportUbos).
     *  - Storage is fixed-size for vkcfg::kMaxFramesInFlight.
     *
     * Contents (per viewport, per frameIndex):
     *  - cameraBuffers[fi] : CameraUBO     (binding 0, raster + RT).
     *  - lightBuffers[fi]  : GpuLightsUBO  (binding 1, raster + RT).
     *  - uboSets[fi]       : DescriptorSet for set=0 (frame globals).
     *
     * Only indices [0..m_framesInFlight-1] are valid for the current device.
     */
    struct ViewportUboState
    {
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     cameraBuffers = {}; ///< per-frame camera UBO (binding 0)
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     lightBuffers  = {}; ///< per-frame Lights UBO (binding 1)
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> uboSets       = {}; ///< per-frame frame-globals set (set = 0)
    };

private:
    // ============================================================
    // Raster helpers (implementation details)
    // ============================================================

    bool createPipelineLayout() noexcept;

    bool createDescriptors(uint32_t framesInFlight);
    bool createPipelines(VkRenderPass renderPass);
    void destroyPipelines() noexcept;

    ViewportUboState& ensureViewportUboState(Viewport* vp, uint32_t frameIndex);

    // Materials (raster + RT path)
    void uploadMaterialsToGpu(const std::vector<Material>& materials,
                              TextureHandler&              texHandler,
                              uint32_t                     frameIndex,
                              const RenderFrameContext&    fc);

    // Grid / overlays / selection (raster path)
    void drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene);
    void ensureOverlayVertexCapacity(std::size_t requiredVertexCount);
    void ensureOverlayFillVertexCapacity(std::size_t requiredVertexCount);
    void drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene);

    // Lights (shared helper for raster + RT)
    void updateViewportLightsUbo(Viewport* vp, Scene* scene, uint32_t frameIndex);

private:
    // ============================================================
    // RT per-viewport state (images, RT set, SSBO, scratch)
    // ============================================================

    struct RtImagePerFrame
    {
        VkImage        image     = VK_NULL_HANDLE;
        VkDeviceMemory memory    = VK_NULL_HANDLE;
        VkImageView    view      = VK_NULL_HANDLE;
        uint32_t       width     = 0;
        uint32_t       height    = 0;
        bool           needsInit = true;
    };

    /**
     * @brief RT state for a single viewport (per-viewport bucket).
     *
     * Scope:
     *  - One RtViewportState per Viewport* (Renderer::m_rtViewports).
     *  - Storage is fixed-size for vkcfg::kMaxFramesInFlight.
     *
     * Contents (per viewport, per frameIndex):
     *  - sets[fi]               : RT-only descriptor set (set = 2).
     *  - instanceDataBuffers[fi]: RtInstanceData SSBO (set=2/binding=3).
     *  - images[fi]             : RT output image + view (storage/sampled).
     *  - scratchBuffers[fi]     : RT scratch buffer (AS build) for this viewport.
     *  - scratchSizes[fi]       : capacity-tracking for scratchBuffers.
     *
     * Only indices [0..m_framesInFlight-1] are valid for the current device.
     */
    struct RtViewportState
    {
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight>   sets                = {}; ///< per-frame RT descriptor set (set=2)
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>       instanceDataBuffers = {}; ///< per-frame instance SSBO (binding=3 in set=2)
        std::array<RtImagePerFrame, vkcfg::kMaxFramesInFlight> images              = {}; ///< per-frame RT output image + view

        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>    scratchBuffers = {}; ///< per-frame RT scratch (AS build) for this viewport
        std::array<VkDeviceSize, vkcfg::kMaxFramesInFlight> scratchSizes   = {}; ///< per-frame scratch capacity (bytes)

        uint32_t cachedW = 0; ///< Last RT width used to create images.
        uint32_t cachedH = 0; ///< Last RT height used to create images.

        /// Destroy all device resources for this viewport (all frames).
        void destroyDeviceResources(const VulkanContext& ctx, uint32_t framesInFlight) noexcept;
    };

    RtViewportState& ensureRtViewportState(Viewport* vp, uint32_t frameIndex);
    bool             ensureRtOutputImages(RtViewportState&          s,
                                          const RenderFrameContext& fc,
                                          uint32_t                  w,
                                          uint32_t                  h);
    void             destroyRtOutputImages(RtViewportState& s, uint32_t framesInFlight) noexcept;

private:
    // ============================================================
    // RT: pipelines, descriptors, dispatch
    // ============================================================

    bool initRayTracingResources();
    bool createRtPresentPipeline(VkRenderPass rp);
    void destroyRtPresentPipeline() noexcept;

    void renderRayTrace(Viewport* vp, Scene* scene, const RenderFrameContext& fc);

    bool ensureRtScratch(Viewport* vp, const RenderFrameContext& fc, VkDeviceSize bytes) noexcept;

private:
    // ============================================================
    // RT: Acceleration structures (scene-level)
    // ============================================================

    bool ensureSceneTlas(Viewport* vp, Scene* scene, const RenderFrameContext& fc) noexcept;
    void writeRtTlasDescriptor(Viewport* vp, uint32_t frameIndex) noexcept;
    void clearRtTlasDescriptor(Viewport* vp, uint32_t frameIndex) noexcept;

    bool ensureMeshBlas(Viewport*                           vp,
                        SceneMesh*                          sm,
                        const render::geom::RtMeshGeometry& geo,
                        const RenderFrameContext&           fc) noexcept;

    void destroyRtBlasFor(SceneMesh* sm, const RenderFrameContext& fc) noexcept;
    void destroyAllRtBlas() noexcept;

    void destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept;
    void destroyAllRtTlasFrames() noexcept;

private:
    // ============================================================
    // Context / frame config (device lifetime)
    // ============================================================

    VulkanContext m_ctx = {};

    uint32_t m_framesInFlight = 1;

    static constexpr uint32_t kMaxViewports = 8;

private:
    // ============================================================
    // Raster pipelines / layout (device + swapchain lifetime)
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
    // Raster descriptors / frame globals (device lifetime)
    // ============================================================

    /**
     * @brief Frame globals DescriptorSetLayout (set = 0).
     *
     * Binding 0 : CameraUBO     (raster + RT)
     * Binding 1 : GpuLightsUBO  (raster + RT)
     */
    DescriptorSetLayout m_descriptorSetLayout = {};

    /**
     * @brief Material DescriptorSetLayout (set = 1).
     *
     * Binding 0 : materials SSBO
     * Binding 1 : sampler array (texture table)
     */
    DescriptorSetLayout m_materialSetLayout = {};

    DescriptorPool m_descriptorPool = {};

    std::unordered_map<Viewport*, ViewportUboState> m_viewportUbos = {};

    std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> m_materialSets = {};

    HeadlightSettings m_headlight        = {};
    LightingSettings  m_lightingSettings = {};

private:
    // ============================================================
    // Materials SSBO (device lifetime, per-frame descriptor binding)
    // ============================================================

    std::array<GpuBuffer, vkcfg::kMaxFramesInFlight> m_materialBuffers = {};

    std::uint32_t m_materialCount = 0;

    std::array<std::uint64_t, vkcfg::kMaxFramesInFlight> m_materialCounterPerFrame = {};

private:
    // ============================================================
    // Overlay vertex buffers (device lifetime)
    // ============================================================

    GpuBuffer   m_overlayVertexBuffer   = {};
    std::size_t m_overlayVertexCapacity = 0;

    GpuBuffer   m_overlayFillVertexBuffer   = {};
    std::size_t m_overlayFillVertexCapacity = 0;

private:
    // ============================================================
    // Ray tracing (DrawMode::RAY_TRACE) - device + swapchain lifetime
    // ============================================================

    DescriptorSetLayout m_rtSetLayout = {};
    DescriptorPool      m_rtPool      = {};

    VkSampler m_rtSampler = VK_NULL_HANDLE;

    VkFormat m_rtFormat = VK_FORMAT_R8G8B8A8_UNORM;

    vkrt::RtPipeline        m_rtPipeline = {};
    vkrt::RtSbt             m_rtSbt      = {};
    vkrt::RtPresentPipeline m_rtPresent  = {};

    VkCommandPool m_rtUploadPool = VK_NULL_HANDLE;

    std::unordered_map<Viewport*, RtViewportState> m_rtViewports = {};

private:
    // ============================================================
    // RT Acceleration Structures (BLAS per mesh, TLAS per frame)
    // ============================================================

    struct RtBlas
    {
        VkAccelerationStructureKHR as      = VK_NULL_HANDLE;
        VkDeviceAddress            address = 0;

        GpuBuffer asBuffer = {};

        VkBuffer posBuffer = VK_NULL_HANDLE;
        uint32_t posCount  = 0;
        VkBuffer idxBuffer = VK_NULL_HANDLE;
        uint32_t idxCount  = 0;

        int32_t  subdivLevel   = 0;
        uint64_t topoCounter   = 0;
        uint64_t deformCounter = 0;

        uint64_t buildKey = 0;
    };

    struct RtTlasFrame
    {
        VkAccelerationStructureKHR as = VK_NULL_HANDLE;

        GpuBuffer       buffer  = {};
        VkDeviceAddress address = 0;

        GpuBuffer instanceBuffer  = {};
        GpuBuffer instanceStaging = {};

        uint64_t buildKey = 0;
    };

    std::unordered_map<SceneMesh*, RtBlas> m_rtBlas = {};

    std::array<RtTlasFrame, vkcfg::kMaxFramesInFlight> m_rtTlasFrames = {};

private:
    // ============================================================
    // RT change tracking (TLAS invalidation)
    // ============================================================

    SysCounterPtr m_rtTlasChangeCounter;
    SysMonitor    m_rtTlasChangeMonitor;

    std::unordered_set<class SysMesh*> m_rtTlasLinkedMeshes = {};

private:
    // ============================================================
    // Misc helpers
    // ============================================================

    template<typename Fn>
    void forEachVisibleMesh(Scene* scene, Fn&& fn);
};
