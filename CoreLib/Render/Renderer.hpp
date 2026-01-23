//============================================================
// Renderer.hpp
//============================================================
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
#include "RenderGeometry.hpp" // for render::geom.. revise
#include "RtPipeline.hpp"
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
 * NOTE (important refactor):
 *  - RT resources are now PER-VIEWPORT (and per-frame inside each viewport).
 *    This avoids descriptor/image/camera buffer clashes when multiple viewports
 *    render RT in the same frame.
 *
 * Device-level resources (created in initDevice(), destroyed in shutdown()):
 *  - descriptor set layouts + descriptor pools
 *  - per-viewport, per-frame MVP uniform buffers + set=0 descriptor sets
 *  - pipeline layout
 *  - material SSBO (host-visible)
 *  - overlay vertex buffer (host-visible)
 *  - optional device-level grid resources
 *
 * Swapchain-level resources (created in initSwapchain(rp), destroyed in destroySwapchainResources()):
 *  - graphics pipelines (created against a specific VkRenderPass)
 *  - grid swapchain pipelines/resources
 *  - RT present pipeline (fullscreen) (created against render pass)
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

    bool initDevice(const VulkanContext& ctx);
    bool initSwapchain(VkRenderPass renderPass);
    void destroySwapchainResources() noexcept;
    void shutdown() noexcept;

    void idle(Scene* scene);
    void waitDeviceIdle() noexcept;

    // ------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------

    void updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex);
    void renderPrePass(Viewport* vp, Scene* scene, const RenderFrameContext& fc);
    void render(Viewport* vp, Scene* scene, const RenderFrameContext& fc);
    void drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays);

    // ------------------------------------------------------------
    // Internal structs (host-side)
    // ------------------------------------------------------------

    struct MvpUBO
    {
        glm::mat4 proj;
        glm::mat4 view;
    };

    struct PushConstants
    {
        glm::mat4 model;
        glm::vec4 color;
        glm::vec4 overlayParams;
    };
    static_assert(sizeof(PushConstants) == 96);

    struct OverlayVertex
    {
        glm::vec3 pos;
        float     thickness;
        glm::vec4 color;
    };

    struct alignas(16) RtCameraUBO
    {
        glm::mat4 invViewProj = {}; // 64
        glm::vec4 camPos      = {}; // 16
        glm::vec4 clearColor  = {}; // 16
    };
    static_assert(sizeof(RtCameraUBO) == 96);
    static_assert(alignof(RtCameraUBO) == 16);

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

    struct ViewportUboState
    {
        std::vector<GpuBuffer>     mvpBuffers;
        std::vector<DescriptorSet> uboSets;
    };

private:
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
    // RT per-viewport state
    // ------------------------------------------------------------

    struct RtImagePerFrame
    {
        VkImage        image     = VK_NULL_HANDLE;
        VkDeviceMemory memory    = VK_NULL_HANDLE;
        VkImageView    view      = VK_NULL_HANDLE;
        uint32_t       width     = 0;
        uint32_t       height    = 0;
        bool           needsInit = true;
    };

    struct RtViewportState
    {
        std::vector<DescriptorSet>   sets;                ///< per-frame RT descriptor set (set=0 for RT path)
        std::vector<GpuBuffer>       cameraBuffers;       ///< per-frame RT camera UBO (binding=2)
        std::vector<GpuBuffer>       instanceDataBuffers; ///< per-frame instance SSBO (binding=4)
        std::vector<RtImagePerFrame> images;              ///< per-frame RT output image + view

        std::vector<GpuBuffer>    scratchBuffers; ///< per-frame RT scratch (AS build)
        std::vector<VkDeviceSize> scratchSizes;   ///< per-frame scratch capacity (bytes)

        uint32_t cachedW = 0;
        uint32_t cachedH = 0;

        void destroyDeviceResources(const VulkanContext& ctx) noexcept;
    };

    RtViewportState& ensureRtViewportState(Viewport* vp);
    bool             ensureRtOutputImages(RtViewportState& s, uint32_t w, uint32_t h);
    void             destroyRtOutputImages(RtViewportState& s) noexcept;

    // ------------------------------------------------------------
    // RT related (device-level + scene-level AS)
    // ------------------------------------------------------------

    bool initRayTracingResources();
    bool createRtPresentPipeline(VkRenderPass rp);
    void destroyRtPresentPipeline() noexcept;

    bool ensureSceneTlas(Viewport* vp, Scene* scene, const RenderFrameContext& fc) noexcept;
    void writeRtTlasDescriptor(Viewport* vp, uint32_t frameIndex) noexcept;

    bool ensureMeshBlas(Viewport* vp, SceneMesh* sm, const render::geom::RtMeshGeometry& geo, const RenderFrameContext& fc) noexcept;

    void destroyRtBlasFor(SceneMesh* sm, const RenderFrameContext& fc) noexcept;
    void destroyAllRtBlas() noexcept;

    void destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept;
    void destroyAllRtTlasFrames() noexcept;

    void clearRtTlasDescriptor(Viewport* vp, uint32_t frameIndex) noexcept;

    void renderRayTrace(Viewport* vp, Scene* scene, const RenderFrameContext& fc);

    bool ensureRtScratch(Viewport* vp, const RenderFrameContext& fc, VkDeviceSize bytes) noexcept;

private:
    // ------------------------------------------------------------
    // Context / frame config
    // ------------------------------------------------------------

    VulkanContext m_ctx{};
    uint32_t      m_framesInFlight = 1;

    // ------------------------------------------------------------
    // Pipelines / layout
    // ------------------------------------------------------------

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_pipelineSolid      = VK_NULL_HANDLE;
    VkPipeline m_pipelineShaded     = VK_NULL_HANDLE;
    VkPipeline m_pipelineDepthOnly  = VK_NULL_HANDLE;
    VkPipeline m_pipelineWire       = VK_NULL_HANDLE;
    VkPipeline m_pipelineEdgeHidden = VK_NULL_HANDLE;

    VkPipeline m_pipelineEdgeDepthBias = VK_NULL_HANDLE;

    VkPipeline m_pipelineSelVert       = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelEdge       = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelPoly       = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelVertHidden = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelEdgeHidden = VK_NULL_HANDLE;
    VkPipeline m_pipelineSelPolyHidden = VK_NULL_HANDLE;

    VkPipeline m_overlayLinePipeline = VK_NULL_HANDLE;

    std::unique_ptr<GridRendererVK> m_grid;

    // ------------------------------------------------------------
    // Ray tracing (DrawMode::RAY_TRACE)
    // ------------------------------------------------------------

    // RT descriptor set layout (set=0 for RT path only)
    DescriptorSetLayout m_rtSetLayout;

    // IMPORTANT: pool is shared, sets are per-viewport-per-frame allocated lazily.
    DescriptorPool m_rtPool;

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

    // PER-VIEWPORT RT state
    std::unordered_map<Viewport*, RtViewportState> m_rtViewports;

    static constexpr uint32_t kMaxViewports = 8;

    // ------------------------------------------------------------
    // Descriptors (raster)
    // ------------------------------------------------------------

    DescriptorSetLayout m_descriptorSetLayout;
    DescriptorSetLayout m_materialSetLayout;
    DescriptorPool      m_descriptorPool;

    std::unordered_map<Viewport*, ViewportUboState> m_viewportUbos;
    std::vector<DescriptorSet>                      m_materialSets;

    // ------------------------------------------------------------
    // Materials SSBO
    // ------------------------------------------------------------

    GpuBuffer     m_materialBuffer;
    std::uint32_t m_materialCount      = 0;
    uint64_t      m_curMaterialCounter = 0;

    // ------------------------------------------------------------
    // Overlay vertex buffer
    // ------------------------------------------------------------

    GpuBuffer   m_overlayVertexBuffer;
    std::size_t m_overlayVertexCapacity = 0;

    // ------------------------------------------------------------
    // RT Acceleration Structures (BLAS per mesh, TLAS per frame)
    // ------------------------------------------------------------

    struct RtBlas
    {
        VkAccelerationStructureKHR as      = VK_NULL_HANDLE;
        VkDeviceAddress            address = 0;

        GpuBuffer asBuffer;

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
    std::vector<RtTlasFrame>               m_rtTlasFrames;

    // ------------------------------------------------------------
    // RT change tracking (TLAS invalidation)
    // ------------------------------------------------------------

    SysCounterPtr m_rtTlasChangeCounter;
    SysMonitor    m_rtTlasChangeMonitor;

    std::unordered_set<class SysMesh*> m_rtTlasLinkedMeshes;

    template<typename Fn>
    void forEachVisibleMesh(class Scene* scene, Fn&& fn);
};
