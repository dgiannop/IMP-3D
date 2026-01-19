#pragma once

#include <SysCounter.hpp>
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
#include "GridRendererVK.hpp"
#include "Material.hpp"
#include "OverlayHandler.hpp"
#include "RtPipeline.hpp"
#include "RtSbt.hpp"
#include "VulkanContext.hpp"

class Scene;
class SceneMesh;
class TextureHandler;
class Viewport;

constexpr uint32_t kMaxTextureCount = 512;

/**
 * @brief Minimal Vulkan renderer for the app.
 *
 * Lifetime model (important for swapchain recreation):
 *
 * Device-level resources (created in initDevice(), destroyed in shutdown()):
 *  - descriptor set layouts + descriptor pool + per-frame descriptor sets
 *  - per-viewport, per-frame MVP uniform buffers + set=0 descriptor sets
 *  - pipeline layout
 *  - material SSBO (host-visible)
 *  - overlay vertex buffer (host-visible)
 *  - optional device-level grid resources (vertex buffer, etc.)
 *
 * Swapchain-level resources (created in initSwapchain(rp), destroyed in destroySwapchainResources()):
 *  - graphics pipelines (they are created against a specific VkRenderPass)
 *  - grid swapchain pipelines/resources if used
 *
 * Usage:
 *  - initDevice(ctx) once after VkDevice is ready
 *  - initSwapchain(renderPass) whenever swapchain (and therefore render pass) changes
 *  - renderPrePass(vp, cmd, scene, frameIndex) BEFORE beginRenderPass (RT dispatch)
 *  - render(cmd, vp, scene, frameIndex) per frame (raster OR RT present)
 *  - destroySwapchainResources() on swapchain teardown / resize
 *  - shutdown() on app shutdown / device teardown
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

    // ------------------------------------------------------------
    // Lifetime
    // ------------------------------------------------------------

    /**
     * @brief Create device-level resources (does not depend on swapchain).
     */
    bool initDevice(const VulkanContext& ctx);

    /**
     * @brief Create swapchain-level resources (pipelines) for a given render pass.
     *
     * @param renderPass The render pass used by the swapchain rendering path.
     */
    bool initSwapchain(VkRenderPass renderPass);

    /**
     * @brief Destroy swapchain-level resources (pipelines, per-swapchain grid resources).
     *
     * Safe to call multiple times.
     */
    void destroySwapchainResources() noexcept;

    /**
     * @brief Destroy everything (device-level + swapchain-level).
     *
     * After shutdown(), the renderer is back to default state.
     */
    void shutdown() noexcept;

    /**
     * @brief Per-frame housekeeping that is safe to do outside render passes.
     *
     * Call once per frame (e.g., from Core/Scene idle tick).
     */
    void idle(Scene* scene);

    /**
     * @brief Convenience for debugging / teardown ordering.
     */
    void waitDeviceIdle() noexcept;

    // ------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------

    /**
     * @brief Update the material texture descriptor table for a given frame.
     *
     * This must be called after textures are created/changed.
     * It updates set=1 binding=1 with an array of combined image samplers.
     */
    void updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex);

    /**
     * @brief Render the entire scene for a viewport into the active render pass.
     *
     * For DrawMode::RAY_TRACE this does NOT dispatch rays. It only draws the fullscreen
     * present pass sampling the RT output image (RtPresent.*).
     *
     * RT dispatch happens in renderPrePass().
     */
    void render(VkCommandBuffer cmd, Viewport* vp, Scene* scene, uint32_t frameIndex);

    /**
     * @brief Draw overlay line primitives from OverlayHandler.
     *
     * NOTE: This refers to *tool/gizmo overlay primitives* (Overlay.vert/.geom/.frag),
     * not the solid-mode wireframe depth-bias edge pass.
     */
    void drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays);

    // ------------------------------------------------------------
    // RT related
    // ------------------------------------------------------------

    /**
     * @brief Run RT work BEFORE the render pass begins (ray dispatch writes the RT output image).
     *
     * This requires Scene access because RT needs per-mesh GPU buffers and BLAS/TLAS.
     */
    void renderPrePass(Viewport* vp, VkCommandBuffer cmd, Scene* scene, uint32_t frameIndex);

    // ------------------------------------------------------------
    // Internal structs (host-side)
    // ------------------------------------------------------------

    /// UBO layout (per frame).
    struct MvpUBO
    {
        glm::mat4 proj;
        glm::mat4 view;
    };

    /// Push constants shared by mesh + overlay pipelines.
    struct PushConstants
    {
        glm::mat4 model;         ///< object model matrix
        glm::vec4 color;         ///< generic tint / line color / selection color
        glm::vec4 overlayParams; ///< currently: xy = viewport size in pixels, z/w unused in the non-GS path
    };
    static_assert(sizeof(PushConstants) == 96);

    /// Overlay vertex format (line list; expanded/thickened in shader).
    struct OverlayVertex
    {
        glm::vec3 pos;
        float     thickness; // in pixels
        glm::vec4 color;
    };

    /// RT camera UBO (std140 friendly).
    struct alignas(16) RtCameraUBO
    {
        glm::mat4 invViewProj;
        glm::vec4 camPos; // xyz = world pos
    };
    static_assert(sizeof(RtCameraUBO) == 80);

    struct RtInstanceData
    {
        uint64_t posAdr; // vec4 positions (shader-readable)
        uint64_t idxAdr; // padded uvec4 triangle indices (shader-readable)
        uint64_t nrmAdr; // vec4 corner normals (shader-readable)

        uint64_t uvAdr; // vec4 corner uvs (shader-readable)  <-- NEW

        uint32_t triCount;
        uint32_t pad0;
        uint32_t pad1;
        uint32_t pad2;
    };

    static_assert(sizeof(RtInstanceData) == 48);
    static_assert(alignof(RtInstanceData) == alignof(uint64_t));

    /// Per-viewport UBO + set=0 (fixes multi-swapchain frameIndex desync).
    struct ViewportUboState
    {
        std::vector<GpuBuffer>     mvpBuffers; ///< one host-visible UBO per frame
        std::vector<DescriptorSet> uboSets;    ///< per-frame set=0
    };

    /**
     * @brief RT geometry source chosen for a SceneMesh.
     */
    struct RtMeshGeometry
    {
        // BLAS build inputs (shared)
        VkBuffer buildPosBuffer = VK_NULL_HANDLE; // vec3 positions
        uint32_t buildPosCount  = 0;

        VkBuffer buildIndexBuffer = VK_NULL_HANDLE; // tight uint32 list (3 per tri)
        uint32_t buildIndexCount  = 0;              // indexCount (triCount*3)

        // Shader shading inputs (padded)
        VkBuffer shadePosBuffer = VK_NULL_HANDLE; // vec4 positions (shader-readable)
        uint32_t shadePosCount  = 0;

        VkBuffer shaderIndexBuffer = VK_NULL_HANDLE; // uvec4 per tri (shader-readable)
        uint32_t shaderTriCount    = 0;              // triCount (not *3)

        // Shader shading normals (padded, per-corner)
        VkBuffer shadeNrmBuffer = VK_NULL_HANDLE; // vec4 corner normals (triCount*3)
        uint32_t shadeNrmCount  = 0;              // cornerCount

        // NEW: Shader shading uvs (padded, per-corner)
        VkBuffer shadeUvBuffer = VK_NULL_HANDLE; // vec4 corner uvs (triCount*3) [uv in .xy]
        uint32_t shadeUvCount  = 0;              // cornerCount

        bool valid() const noexcept
        {
            return buildPosBuffer != VK_NULL_HANDLE && buildPosCount > 0 &&
                   buildIndexBuffer != VK_NULL_HANDLE && buildIndexCount > 0;
        }

        bool shaderValid() const noexcept
        {
            return shadePosBuffer != VK_NULL_HANDLE && shadePosCount > 0 &&
                   shaderIndexBuffer != VK_NULL_HANDLE && shaderTriCount > 0 &&
                   shadeNrmBuffer != VK_NULL_HANDLE && shadeNrmCount > 0 &&
                   shadeUvBuffer != VK_NULL_HANDLE && shadeUvCount > 0;
        }
    };

    // ------------------------------------------------------------
    // Helpers: lifetime
    // ------------------------------------------------------------

    bool createDescriptors(uint32_t framesInFlight);
    bool createPipelineLayout() noexcept;

    bool createPipelines(VkRenderPass renderPass);
    void destroyPipelines() noexcept;

    // ------------------------------------------------------------
    // Helpers: materials
    // ------------------------------------------------------------

    void uploadMaterialsToGpu(const std::vector<Material>& materials,
                              TextureHandler&              texHandler,
                              uint32_t                     frameIndex);

    // ------------------------------------------------------------
    // Helpers: overlays + selection
    // ------------------------------------------------------------

    void ensureOverlayVertexCapacity(std::size_t requiredVertexCount);
    void drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene);

    // ------------------------------------------------------------
    // Helpers: per-viewport UBO state
    // ------------------------------------------------------------

    ViewportUboState& ensureViewportUboState(Viewport* vp);

    // ------------------------------------------------------------
    // Helpers: grid
    // ------------------------------------------------------------

    void drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene);

private:
    // ------------------------------------------------------------
    // RT related
    // ------------------------------------------------------------

    bool initRayTracingResources();                // called from initDevice()
    bool createRtPresentPipeline(VkRenderPass rp); // called from initSwapchain()

    bool ensureSceneTlas(Scene* scene, VkCommandBuffer cmd, uint32_t frameIndex) noexcept;
    // Writes set=0 binding=3 (uTlas) for the RT descriptor set of this frame.
    void writeRtTlasDescriptor(uint32_t frameIndex) noexcept;

    bool ensureMeshBlas(SceneMesh* sm, const RtMeshGeometry& geo, VkCommandBuffer cmd) noexcept;

    void destroyRtPresentPipeline() noexcept;

    void destroyRtBlasFor(SceneMesh* sm) noexcept;
    void destroyAllRtBlas() noexcept;

    void destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept;
    void destroyAllRtTlasFrames() noexcept;

    void renderRayTrace(Viewport* vp, VkCommandBuffer cmd, Scene* scene, uint32_t frameIndex);

    // ------------------------------------------------------------
    // RT output images (PER-FRAME)
    // ------------------------------------------------------------

    struct RtImagePerFrame
    {
        VkImage        image     = VK_NULL_HANDLE;
        VkDeviceMemory memory    = VK_NULL_HANDLE;
        VkImageView    view      = VK_NULL_HANDLE;
        uint32_t       width     = 0;
        uint32_t       height    = 0;
        bool           needsInit = true; // if true: UNDEFINED -> GENERAL on first use
    };

    bool ensureRtOutputImages(uint32_t w, uint32_t h);
    void destroyRtOutputImages() noexcept;

    /**
     * @brief Choose the correct RT geometry buffers for a mesh (coarse vs subdiv).
     */
    RtMeshGeometry selectRtGeometry(SceneMesh* sm) noexcept;

    // RT scratch reused for BLAS/TLAS builds (must outlive command buffer execution)
    GpuBuffer    m_rtScratch     = {};
    VkDeviceSize m_rtScratchSize = 0;

    bool ensureRtScratch(VkDeviceSize bytes) noexcept;

private:
    // ------------------------------------------------------------
    // Context / frame config
    // ------------------------------------------------------------

    VulkanContext m_ctx{};              ///< device, physicalDevice, queues, framesInFlight, etc.
    uint32_t      m_framesInFlight = 1; ///< cached from m_ctx.framesInFlight (>=1)

    // ------------------------------------------------------------
    // Pipelines / layout
    // ------------------------------------------------------------

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE; ///< device-level: depends on descriptors + push constants

    // Swapchain-level pipelines (must be recreated when render pass changes)
    VkPipeline m_pipelineSolid      = VK_NULL_HANDLE; ///< solid triangles (SolidDraw.vert/.frag)
    VkPipeline m_pipelineShaded     = VK_NULL_HANDLE; ///< shaded triangles (ShadedDraw.vert/.frag)
    VkPipeline m_pipelineDepthOnly  = VK_NULL_HANDLE; ///< depth-only triangle prepass (SolidDraw.vert)
    VkPipeline m_pipelineWire       = VK_NULL_HANDLE; ///< wireframe visible edges (LEQUAL) (Wireframe.vert/.frag)
    VkPipeline m_pipelineEdgeHidden = VK_NULL_HANDLE; ///< wireframe hidden edges (GREATER) (Wireframe.vert/.frag)

    // Solid-mode edge overlay (depth-bias vertex shader).
    VkPipeline m_pipelineEdgeDepthBias = VK_NULL_HANDLE;

    // Selection pipelines (visible + hidden)
    VkPipeline m_pipelineSelVert       = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelEdge       = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelPoly       = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelVertHidden = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelEdgeHidden = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelPolyHidden = VK_NULL_HANDLE;

    // Overlay pipeline (tool/gizmo overlays) (Overlay.vert/.geom/.frag)
    VkPipeline m_overlayLinePipeline = VK_NULL_HANDLE;

    // Optional grid renderer
    std::unique_ptr<GridRendererVK> m_grid;

    // ------------------------------------------------------------
    // Ray tracing (DrawMode::RAY_TRACE)
    // ------------------------------------------------------------

    // RT descriptor set (set=0 for RT path only)
    // binding0 storage image, binding1 combined sampler, binding2 camera UBO, binding3 TLAS
    DescriptorSetLayout        m_rtSetLayout;
    std::vector<DescriptorSet> m_rtSets;
    DescriptorPool             m_rtPool;

    // RT output images (one per frame in flight)
    std::vector<RtImagePerFrame> m_rtImages;

    // Shared sampler + format
    VkSampler m_rtSampler = VK_NULL_HANDLE;
    VkFormat  m_rtFormat  = VK_FORMAT_R8G8B8A8_UNORM;

    // RT pipeline + SBT
    vkrt::RtPipeline m_rtPipeline;
    vkrt::RtSbt      m_rtSbt;

    // Present (swapchain-level)
    VkPipeline       m_rtPresentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_rtPresentLayout   = VK_NULL_HANDLE;

    // Upload helpers for SBT
    VkCommandPool m_rtUploadPool = VK_NULL_HANDLE;

    // One host-visible RT camera UBO per frame
    std::vector<GpuBuffer> m_rtCameraBuffers;

    // One host-visible RT instance-data SSBO per frame (binding=4).
    std::vector<GpuBuffer> m_rtInstanceDataBuffers;

    // ------------------------------------------------------------
    // Descriptors
    // ------------------------------------------------------------

    DescriptorSetLayout m_descriptorSetLayout; ///< set=0 layout (MVP UBO)
    DescriptorSetLayout m_materialSetLayout;   ///< set=1 layout (materials SSBO + sampler table)
    DescriptorPool      m_descriptorPool;      ///< shared pool for all sets

    // set=0 is per-viewport-per-frame, allocated lazily from the shared pool.
    std::unordered_map<Viewport*, ViewportUboState> m_viewportUbos;

    // set=1 (materials) is per-frame and shared across viewports.
    std::vector<DescriptorSet> m_materialSets; ///< per-frame set=1

    // ------------------------------------------------------------
    // Materials SSBO (host visible; updated on demand)
    // ------------------------------------------------------------

    GpuBuffer     m_materialBuffer; ///< SSBO holding GpuMaterial[]
    std::uint32_t m_materialCount      = 0;
    uint64_t      m_curMaterialCounter = 0;

    // ------------------------------------------------------------
    // Overlay vertex buffer (device-level, host visible)
    // ------------------------------------------------------------

    GpuBuffer   m_overlayVertexBuffer;
    std::size_t m_overlayVertexCapacity = 0; ///< in vertices

    // ------------------------------------------------------------
    // RT Acceleration Structures (BLAS per mesh, TLAS per frame)
    // ------------------------------------------------------------

    struct RtBlas
    {
        VkAccelerationStructureKHR as      = VK_NULL_HANDLE;
        VkDeviceAddress            address = 0;

        // BLAS backing storage (REQUIRED)
        GpuBuffer asBuffer;

        // Geometry signature (used to decide rebuild)
        VkBuffer posBuffer = VK_NULL_HANDLE;
        uint32_t posCount  = 0;
        VkBuffer idxBuffer = VK_NULL_HANDLE;
        uint32_t idxCount  = 0;

        int32_t  subdivLevel   = 0;
        uint64_t topoCounter   = 0;
        uint64_t deformCounter = 0;

        uint64_t buildKey = 0;
    };

    /**
     * @brief Per-frame TLAS. Keeping these per-frame avoids invalidating device addresses
     * recorded in in-flight command buffers (instance buffers and TLAS storage).
     *
     * IMPORTANT lifetime rule:
     *  - Never destroy/recreate a frameIndex slot until that frame's fence has completed.
     *  - Your outer "frames in flight" fence handling provides that guarantee.
     */
    struct RtTlasFrame
    {
        VkAccelerationStructureKHR as = VK_NULL_HANDLE;

        // Device-local storage backing the TLAS object (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR).
        GpuBuffer       buffer  = {};
        VkDeviceAddress address = 0;

        // Device-local instance buffer used as build input for this TLAS.
        // Must remain alive until the command buffer that builds TLAS has finished executing.
        GpuBuffer instanceBuffer  = {}; // VkAccelerationStructureInstanceKHR[]
        GpuBuffer instanceStaging = {}; // host-visible staging (transfer src)

        // Change detection key to skip rebuilds when nothing changed.
        uint64_t buildKey = 0;
    };

    std::unordered_map<SceneMesh*, RtBlas> m_rtBlas = {};
    std::vector<RtTlasFrame>               m_rtTlasFrames;

    // ------------------------------------------------------------
    // RT change tracking (TLAS invalidation)
    // ------------------------------------------------------------

    SysCounterPtr m_rtTlasChangeCounter; ///< parent for all SysMesh topo/deform counters
    SysMonitor    m_rtTlasChangeMonitor; ///< detects when m_rtTlasChangeCounter changes

    // Tracks which SysMesh counters we've already attached, to avoid repeated addParent calls.
    std::unordered_set<class SysMesh*> m_rtTlasLinkedMeshes;
};
