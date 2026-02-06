//============================================================
// Renderer.hpp
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

constexpr uint32_t kMaxTextureCount = 512;

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
 *      * m_viewportUbos: frame-global raster UBOs (set = 0) for each viewport.
 *      * m_rtViewports: RT images, RT per-frame sets/buffers, RT scratch for
 *        each viewport.
 *
 *  - Per-viewport + frame:
 *      * UBOs for MVP/lights/camera.
 *      * RT camera UBO, RT instance SSBO, RT storage image, RT scratch.
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

    // set=0 binding=0 (raster path) - per-viewport, per-frame UBO
    struct MvpUBO
    {
        glm::mat4 proj = {};
        glm::mat4 view = {};
    };

    // Push constants used by raster + overlays (keep layout stable with shader)
    struct PushConstants
    {
        glm::mat4 model         = {};
        glm::vec4 color         = {};
        glm::vec4 overlayParams = {};
    };
    static_assert(sizeof(PushConstants) == 96);

    // set=0 binding=2 (RT path) - per-viewport, per-frame UBO
    struct RtCameraUBO
    {
        glm::mat4 invViewProj = {}; // CLIP -> WORLD
        glm::mat4 view        = {}; // WORLD -> VIEW
        glm::mat4 invView     = {}; // VIEW -> WORLD
        glm::vec4 camPos      = {}; // world-space camera position
        glm::vec4 clearColor  = {}; // clear color for RT
    };

    static_assert(sizeof(RtCameraUBO) % 16 == 0, "RtCameraUBO must be std140-aligned");

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
     *  - mvpBuffers[fi]   : MvpUBO (binding 0, raster path).
     *  - lightBuffers[fi] : GpuLightsUBO (binding 1, raster + RT).
     *  - uboSets[fi]      : DescriptorSet for set=0 (frame globals).
     *
     * RtCameraUBO (binding 2) is bound into uboSets[fi] from RtViewportState.
     *
     * Only indices [0..m_framesInFlight-1] are valid for the current device.
     */
    struct ViewportUboState
    {
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     mvpBuffers   = {}; ///< per-frame MVP UBO (binding 0)
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     lightBuffers = {}; ///< per-frame Lights UBO (binding 1)
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> uboSets      = {}; ///< per-frame frame-globals set (set = 0)
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
                              uint32_t                     frameIndex);

    // Grid / overlays / selection (raster path)
    void drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene);
    void ensureOverlayVertexCapacity(std::size_t requiredVertexCount);
    void ensureOverlayFillVertexCapacity(std::size_t requiredVertexCount);
    void drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene);

    // Lights (shared helper for raster + RT)
    void updateViewportLightsUbo(Viewport* vp, Scene* scene, uint32_t frameIndex);

private:
    // ============================================================
    // RT per-viewport state (images, RT set, RT UBO/SSBO, scratch)
    // ============================================================

    /**
     * @brief Per-viewport, per-frame RT output image.
     *
     * Scope:
     *  - One entry per frame-in-flight inside RtViewportState::images.
     *  - Destroyed when viewport RT state is destroyed or resized.
     */
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
     *  - cameraBuffers[fi]      : RtCameraUBO (bound into set=0/binding=2).
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
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>       cameraBuffers       = {}; ///< per-frame RT camera UBO (binding=2 in set=0)
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

    /// Immutable Vulkan handles/config for this renderer.
    VulkanContext m_ctx = {};

    /// Active number of frames in flight (clamped to [1..vkcfg::kMaxFramesInFlight]).
    uint32_t m_framesInFlight = 1;

    /// Upper bound for how many Viewports we allocate per-frame descriptor resources for.
    static constexpr uint32_t kMaxViewports = 8;

private:
    // ============================================================
    // Raster pipelines / layout (device + swapchain lifetime)
    // ============================================================

    /// Raster pipeline layout (set=0 frame globals, set=1 materials).
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Graphics pipelines (swapchain lifetime: depend on VkRenderPass).
    GraphicsPipeline m_solidPipeline       = {}; // SolidDraw (triangles, unlit)
    GraphicsPipeline m_shadedPipeline      = {}; // ShadedDraw (triangles, lit)
    GraphicsPipeline m_depthOnlyPipeline   = {}; // depth prepass (triangles, no color)
    GraphicsPipeline m_wirePipeline        = {}; // visible edges (wireframe)
    GraphicsPipeline m_wireHiddenPipeline  = {}; // hidden edges (wireframe, depth GREATER)
    GraphicsPipeline m_wireOverlayPipeline = {}; // solid-mode wire overlay with depth bias

    GraphicsPipeline m_overlayPipeline     = {}; // gizmo / overlay lines
    GraphicsPipeline m_overlayFillPipeline = {}; // gizmo / overlay triangles

    GraphicsPipeline m_selVertPipeline       = {}; // selection verts visible
    GraphicsPipeline m_selEdgePipeline       = {}; // selection edges visible
    GraphicsPipeline m_selPolyPipeline       = {}; // selection polys visible
    GraphicsPipeline m_selVertHiddenPipeline = {}; // selection verts hidden
    GraphicsPipeline m_selEdgeHiddenPipeline = {}; // selection edges hidden
    GraphicsPipeline m_selPolyHiddenPipeline = {}; // selection polys hidden

    /// Grid renderer (owns its own device + swapchain resources internally).
    std::unique_ptr<GridRendererVK> m_grid;

private:
    // ============================================================
    // Raster descriptors / frame globals (device lifetime)
    // ============================================================

    /**
     * @brief Frame globals DescriptorSetLayout (set = 0).
     *
     * Binding 0 : MvpUBO       (raster)
     * Binding 1 : GpuLightsUBO (raster + RT)
     * Binding 2 : RtCameraUBO  (RT only)
     */
    DescriptorSetLayout m_descriptorSetLayout = {};

    /**
     * @brief Material DescriptorSetLayout (set = 1).
     *
     * Binding 0 : materials SSBO
     * Binding 1 : sampler array (texture table)
     */
    DescriptorSetLayout m_materialSetLayout = {};

    /**
     * @brief Shared descriptor pool for set 0 (frame globals) and set 1 (materials).
     *
     * Sizes are allocated for kMaxViewports * framesInFlight frame-global sets
     * plus framesInFlight material sets.
     *
     * Pool sizing uses the ACTIVE frames-in-flight count, but storage is fixed-size.
     */
    DescriptorPool m_descriptorPool = {};

    /**
     * @brief Per-viewport frame-global state (set = 0).
     *
     * Keyed by Viewport*, each value contains fixed-size per-frame storage.
     */
    std::unordered_map<Viewport*, ViewportUboState> m_viewportUbos = {};

    /**
     * @brief Per-frame material descriptor sets (set = 1).
     *
     * Indexed by frameIndex (0..m_framesInFlight-1), independent of viewport.
     * Storage is fixed-size for vkcfg::kMaxFramesInFlight.
     */
    std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> m_materialSets = {};

    /// Modeling headlight state (renderer-owned implementation detail).
    HeadlightSettings m_headlight = {};

    /// Scene-driven lighting policy (headlight/scene lights/exposure/etc).
    LightingSettings m_lightingSettings = {};

private:
    // ============================================================
    // Materials SSBO (device lifetime, per-frame descriptor binding)
    // ============================================================

    /// GPU buffer backing the materials SSBO (set=1, binding=0).
    GpuBuffer m_materialBuffer = {};

    /// Current number of Material entries stored in m_materialBuffer.
    std::uint32_t m_materialCount = 0;

    /// Change-counter snapshot for Scene materials (to avoid redundant uploads).
    std::uint64_t m_curMaterialCounter = 0;

private:
    // ============================================================
    // Overlay vertex buffers (device lifetime)
    // ============================================================

    /// Wide-line overlay vertex buffer (OverlayVertex).
    GpuBuffer   m_overlayVertexBuffer   = {};
    std::size_t m_overlayVertexCapacity = 0;

    /// Filled overlay vertex buffer (OverlayFillVertex).
    GpuBuffer   m_overlayFillVertexBuffer   = {};
    std::size_t m_overlayFillVertexCapacity = 0;

private:
    // ============================================================
    // Ray tracing (DrawMode::RAY_TRACE) - device + swapchain lifetime
    // ============================================================

    /**
     * @brief RT-only DescriptorSetLayout (set = 2).
     *
     * Binding 0 : storage image (raygen writes)
     * Binding 1 : combined image sampler (present, optional raygen)
     * Binding 2 : TLAS (VkAccelerationStructureKHR)
     * Binding 3 : RtInstanceData SSBO
     */
    DescriptorSetLayout m_rtSetLayout = {};

    /// Descriptor pool used to allocate RT per-viewport, per-frame sets (set = 2).
    DescriptorPool m_rtPool = {};

    /// Sampler used by RT present pipeline to sample the RT output image.
    VkSampler m_rtSampler = VK_NULL_HANDLE;

    /// Format of RT output images.
    VkFormat m_rtFormat = VK_FORMAT_R8G8B8A8_UNORM;

    /// RT scene pipeline (raygen/miss/hit).
    vkrt::RtPipeline m_rtPipeline = {};

    /// Shader binding table for m_rtPipeline.
    vkrt::RtSbt m_rtSbt = {};

    /// Ray tracing present pipeline (fullscreen blit from RT output image).
    vkrt::RtPresentPipeline m_rtPresent = {};

    /// Command pool used for RT uploads / SBT builds.
    VkCommandPool m_rtUploadPool = VK_NULL_HANDLE;

    /**
     * @brief Per-viewport RT state (images, RT sets, camera UBO, instance SSBO, scratch).
     *
     * Keyed by Viewport*, each value contains fixed-size per-frame storage.
     */
    std::unordered_map<Viewport*, RtViewportState> m_rtViewports = {};

private:
    // ============================================================
    // RT Acceleration Structures (BLAS per mesh, TLAS per frame)
    // ============================================================

    /**
     * @brief BLAS state for a single SceneMesh.
     *
     * Scope:
     *  - One RtBlas per SceneMesh* (m_rtBlas map).
     *  - Rebuilt when mesh topology/deform or RT geometry changes.
     */
    struct RtBlas
    {
        VkAccelerationStructureKHR as      = VK_NULL_HANDLE;
        VkDeviceAddress            address = 0;

        GpuBuffer asBuffer = {}; ///< Backing buffer for BLAS.

        // Geometry sizes for build key.
        VkBuffer posBuffer = VK_NULL_HANDLE;
        uint32_t posCount  = 0;
        VkBuffer idxBuffer = VK_NULL_HANDLE;
        uint32_t idxCount  = 0;

        int32_t  subdivLevel   = 0;
        uint64_t topoCounter   = 0;
        uint64_t deformCounter = 0;

        uint64_t buildKey = 0; ///< Hash of topo/deform/geometry used to avoid rebuild.
    };

    /**
     * @brief TLAS state for a single frame-in-flight.
     *
     * Scope:
     *  - One RtTlasFrame per frameIndex (m_rtTlasFrames).
     *
     * Contents:
     *  - as              : TLAS handle.
     *  - buffer          : TLAS backing buffer.
     *  - address         : device address for TLAS.
     *  - instanceBuffer  : device-local instance data buffer.
     *  - instanceStaging : host-visible buffer used to upload instances.
     */
    struct RtTlasFrame
    {
        VkAccelerationStructureKHR as = VK_NULL_HANDLE;

        GpuBuffer       buffer  = {};
        VkDeviceAddress address = 0;

        GpuBuffer instanceBuffer  = {};
        GpuBuffer instanceStaging = {};

        uint64_t buildKey = 0; ///< Hash used to detect when TLAS needs rebuild.
    };

    /// BLAS map per SceneMesh* (device lifetime; contents change with scene).
    std::unordered_map<SceneMesh*, RtBlas> m_rtBlas = {};

    /// TLAS frames: fixed-size storage; only [0..m_framesInFlight-1] is used.
    std::array<RtTlasFrame, vkcfg::kMaxFramesInFlight> m_rtTlasFrames = {};

private:
    // ============================================================
    // RT change tracking (TLAS invalidation)
    // ============================================================

    /// Counter used as a parent for mesh topology/deform counters to invalidate TLAS.
    SysCounterPtr m_rtTlasChangeCounter;

    /// Monitor for m_rtTlasChangeCounter, used in Renderer::idle().
    SysMonitor m_rtTlasChangeMonitor;

    /// Meshes whose topology/deform counters have been linked to m_rtTlasChangeCounter.
    std::unordered_set<class SysMesh*> m_rtTlasLinkedMeshes = {};

private:
    // ============================================================
    // Misc helpers
    // ============================================================

    /**
     * @brief Iterate visible SceneMeshes and provide MeshGpuResources.
     *
     * Ensures that each visible SceneMesh has a MeshGpuResources, creating one
     * on demand, then calls the provided functor:
     *
     *   fn(SceneMesh* mesh, MeshGpuResources* gpu)
     */
    template<typename Fn>
    void forEachVisibleMesh(Scene* scene, Fn&& fn);
};
