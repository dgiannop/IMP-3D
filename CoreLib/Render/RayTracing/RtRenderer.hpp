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
#include "RtDenoiser.hpp"
#include "RtPipeline.hpp"
#include "RtPresentPipeline.hpp"
#include "RtSbt.hpp"
#include "VulkanContext.hpp"
#include "VulkanImage.hpp"

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
    [[nodiscard]] bool initDevice(const VulkanContext&  ctx,
                                  VkDescriptorSetLayout set0Layout,
                                  VkDescriptorSetLayout set1Layout);

    void shutdown() noexcept;

public:
    [[nodiscard]] bool initSwapchain(VkRenderPass renderPass);
    void               destroySwapchainResources() noexcept;

public:
    void renderPrePass(Viewport*                 vp,
                       Scene*                    scene,
                       const RenderFrameContext& fc,
                       VkDescriptorSet           set0FrameGlobals,
                       VkDescriptorSet           set1Materials);

    void present(VkCommandBuffer cmd, Viewport* vp, const RenderFrameContext& fc);
    void idle(Scene* scene);

public:
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
    // ---------------------------------------------------------
    // Per-viewport, per-frame state
    // ---------------------------------------------------------

    struct RtViewportState
    {
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> rtSets              = {};
        std::array<DescriptorSet, vkcfg::kMaxFramesInFlight> presentSets         = {};
        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>     instanceDataBuffers = {};

        /// AOV images written by the ray-tracing pipeline each frame.
        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> radianceImages = {};
        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> normalImages   = {};
        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> depthImages    = {};
        std::array<VulkanImage, vkcfg::kMaxFramesInFlight> albedoImages   = {};

        std::array<GpuBuffer, vkcfg::kMaxFramesInFlight>    scratchBuffers = {};
        std::array<VkDeviceSize, vkcfg::kMaxFramesInFlight> scratchSizes   = {};

        uint32_t cachedW = 0;
        uint32_t cachedH = 0;

        void destroyDeviceResources(uint32_t framesInFlight) noexcept;
    };

    RtViewportState& ensureViewportState(Viewport* vp, uint32_t frameIndex);

    [[nodiscard]] bool ensureRtOutputImages(RtViewportState&          s,
                                            const RenderFrameContext& fc,
                                            uint32_t                  w,
                                            uint32_t                  h);

    [[nodiscard]] bool ensureRtScratch(RtViewportState&          s,
                                       const RenderFrameContext& fc,
                                       VkDeviceSize              bytes) noexcept;

private:
    void recordTraceRays(Viewport*                 vp,
                         Scene*                    scene,
                         const RenderFrameContext& fc,
                         VkDescriptorSet           set0FrameGlobals,
                         VkDescriptorSet           set1Materials);

    void writeRtImageDescriptors(RtViewportState& s, uint32_t frameIndex) noexcept;
    void writeRtPresentImageDescriptor(RtViewportState& s, uint32_t frameIndex, VkImageView view) noexcept;
    void writeRtTlasDescriptor(RtViewportState& s, uint32_t frameIndex) noexcept;
    void clearRtTlasDescriptor(RtViewportState& s, uint32_t frameIndex) noexcept;

private:
    struct RtBlas
    {
        VkAccelerationStructureKHR as       = VK_NULL_HANDLE;
        VkDeviceAddress            address  = 0;
        GpuBuffer                  asBuffer = {};
        uint64_t                   buildKey = 0;
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

    [[nodiscard]] bool ensureSceneTlas(Viewport* vp, Scene* scene, const RenderFrameContext& fc) noexcept;

    [[nodiscard]] bool ensureMeshBlas(Viewport*                           vp,
                                      SceneMesh*                          sm,
                                      const render::geom::RtMeshGeometry& geo,
                                      const RenderFrameContext&           fc) noexcept;

    void destroyRtBlasFor(SceneMesh* sm, const RenderFrameContext& fc) noexcept;
    void destroyAllRtBlas() noexcept;

    void destroyRtTlasFrame(uint32_t frameIndex, bool destroyInstanceBuffers) noexcept;
    void destroyAllRtTlasFrames() noexcept;

private:
    VulkanContext m_ctx            = {};
    uint32_t      m_framesInFlight = 1;

    VkFormat  m_rtFormat       = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat  m_rtNormalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat  m_rtDepthFormat  = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat  m_rtAlbedoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkSampler m_rtSampler      = VK_NULL_HANDLE;

private:
    DescriptorSetLayout m_rtSetLayout = {};
    DescriptorPool      m_rtPool      = {};

    vkrt::RtPipeline        m_rtPipeline = {};
    vkrt::RtSbt             m_rtSbt      = {};
    vkrt::RtPresentPipeline m_rtPresent  = {};
    RtDenoiser              m_denoiser   = {};

    VkCommandPool m_rtUploadPool = VK_NULL_HANDLE;

    std::unordered_map<Viewport*, RtViewportState> m_viewports = {};

    std::unordered_map<SceneMesh*, RtBlas>             m_blas       = {};
    std::array<RtTlasFrame, vkcfg::kMaxFramesInFlight> m_tlasFrames = {};

private:
    SysCounterPtr                m_tlasChangeCounter;
    SysMonitor                   m_tlasChangeMonitor;
    std::unordered_set<SysMesh*> m_linkedMeshes = {};
};
