#pragma once

#include <SysCounter.hpp>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vulkan/vulkan.h>

#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "GpuBuffer.hpp"
#include "RtPipeline.hpp"
#include "RtPresentPipeline.hpp"
#include "RtSbt.hpp"
#include "VulkanContext.hpp"

class Scene;
class SceneMesh;
class Viewport;
class SysMesh;
struct RenderFrameContext;

namespace render::geom
{
    struct RtMeshGeometry;
} // namespace render::geom

class RtRenderer final
{
public:
    RtRenderer() noexcept;
    ~RtRenderer() noexcept = default;

    RtRenderer(const RtRenderer&)            = delete;
    RtRenderer& operator=(const RtRenderer&) = delete;

public:
    // ============================================================
    // Device lifetime
    // ============================================================

    // NOTE: RtRenderer does NOT create set=0 or set=1.
    // It only needs their VkDescriptorSetLayout to build its pipeline layout.
    bool initDevice(const VulkanContext&  ctx,
                    VkDescriptorSetLayout set0Layout, // frame globals layout (camera+lights)
                    VkDescriptorSetLayout set1Layout  // materials layout
    );

    void shutdown() noexcept;

public:
    // ============================================================
    // Swapchain lifetime (RT present pipeline depends on render pass)
    // ============================================================

    bool initSwapchain(VkRenderPass renderPass);
    void destroySwapchainResources() noexcept;

public:
    // ============================================================
    // Frame hooks
    // ============================================================

    // OUTSIDE render pass:
    // - build/update BLAS/TLAS as needed
    // - ensure RT output image (per viewport, per frame)
    // - vkCmdTraceRaysKHR
    //
    // Renderer passes the already-built descriptor sets:
    //   set=0 : camera+lights
    //   set=1 : materials+texture table
    void renderPrePass(Viewport*                 vp,
                       Scene*                    scene,
                       const RenderFrameContext& fc,
                       VkDescriptorSet           set0FrameGlobals,
                       VkDescriptorSet           set1Materials);

    // Inside render pass:
    // fullscreen present of the RT output for this viewport+frame
    void present(VkCommandBuffer cmd, Viewport* vp, const RenderFrameContext& fc);

    // TLAS invalidation
    void idle(Scene* scene);

public:
    // ============================================================
    // Shared RT SSBO element layout (std430) — shader-visible
    // ============================================================

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

private:
    // ============================================================
    // Per-viewport/per-frame RT buckets
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

    struct RtViewportState
    {
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight>   sets                = {}; // set=2
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>       instanceDataBuffers = {};
        std::array<RtImagePerFrame, vkcfg::kMaxFramesInFlight> images              = {};

        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>    scratchBuffers = {};
        std::array<VkDeviceSize, vkcfg::kMaxFramesInFlight> scratchSizes   = {};

        uint32_t cachedW = 0;
        uint32_t cachedH = 0;

        void destroyDeviceResources(const VulkanContext& ctx, uint32_t framesInFlight) noexcept;
    };

    RtViewportState& ensureViewportState(Viewport* vp, uint32_t frameIndex);

    bool ensureRtOutputImages(RtViewportState&          s,
                              const RenderFrameContext& fc,
                              uint32_t                  w,
                              uint32_t                  h);

    bool ensureRtScratch(RtViewportState& s, const RenderFrameContext& fc, VkDeviceSize bytes) noexcept;

private:
    // ============================================================
    // Pipelines, descriptors, dispatch
    // ============================================================

    bool initRayTracingResources(VkDescriptorSetLayout set0Layout, VkDescriptorSetLayout set1Layout);
    bool createRtPresentPipeline(VkRenderPass rp);
    void destroyRtPresentPipeline() noexcept;

    void recordTraceRays(Viewport*                 vp,
                         Scene*                    scene,
                         const RenderFrameContext& fc,
                         VkDescriptorSet           set0FrameGlobals,
                         VkDescriptorSet           set1Materials);

    // set=2 writing helpers
    void writeRtImageDescriptors(RtViewportState& s, uint32_t frameIndex) noexcept;
    void writeRtTlasDescriptor(RtViewportState& s, uint32_t frameIndex) noexcept;
    void clearRtTlasDescriptor(RtViewportState& s, uint32_t frameIndex) noexcept;

private:
    // ============================================================
    // Acceleration structures (BLAS per mesh, TLAS per frame)
    // ============================================================

    struct RtBlas
    {
        VkAccelerationStructureKHR as      = VK_NULL_HANDLE;
        VkDeviceAddress            address = 0;

        GpuBuffer asBuffer = {};

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

    bool ensureSceneTlas(Viewport* vp, Scene* scene, const RenderFrameContext& fc) noexcept;

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
    // Context / config
    // ============================================================

    VulkanContext m_ctx            = {};
    uint32_t      m_framesInFlight = 1;

    VkFormat  m_rtFormat  = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkSampler m_rtSampler = VK_NULL_HANDLE;

private:
    // ============================================================
    // RT-only descriptors (set=2) + pipeline/SBT + present pipeline
    // ============================================================

    DescriptorSetLayout m_rtSetLayout = {}; // set=2 layout
    DescriptorPool      m_rtPool      = {}; // pool for set=2 sets

    vkrt::RtPipeline        m_rtPipeline = {};
    vkrt::RtSbt             m_rtSbt      = {};
    vkrt::RtPresentPipeline m_rtPresent  = {};

    VkCommandPool m_rtUploadPool = VK_NULL_HANDLE;

    std::unordered_map<Viewport*, RtViewportState> m_viewports = {};

    std::unordered_map<SceneMesh*, RtBlas>             m_blas       = {};
    std::array<RtTlasFrame, vkcfg::kMaxFramesInFlight> m_tlasFrames = {};

private:
    // ============================================================
    // TLAS invalidation wiring (optional)
    // ============================================================

    SysCounterPtr                m_tlasChangeCounter;
    SysMonitor                   m_tlasChangeMonitor;
    std::unordered_set<SysMesh*> m_linkedMeshes = {};
};
