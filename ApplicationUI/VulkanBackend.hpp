#pragma once

#include <QSize>
#include <QVulkanInstance>
#include <VulkanContext.hpp>
#include <vector>
#include <vulkan/vulkan.h>

class QWindow;

struct ViewportFrame
{
    VkCommandBuffer cmd            = VK_NULL_HANDLE;
    VkFence         fence          = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    VkSemaphore     renderFinished = VK_NULL_HANDLE;
    VkFramebuffer   fb             = VK_NULL_HANDLE;
};

struct ViewportSwapchain
{
    QWindow*     window  = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain   = VK_NULL_HANDLE;
    VkFormat       colorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent      = {};

    // Store what this swapchain was created with (important!)
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkImage>     images = {};
    std::vector<VkImageView> views  = {};

    VkRenderPass renderPass = VK_NULL_HANDLE;

    // --- MSAA color (one per swapchain image) ---
    std::vector<VkImage>        msaaColorImages = {};
    std::vector<VkDeviceMemory> msaaColorMems   = {};
    std::vector<VkImageView>    msaaColorViews  = {};

    // --- Depth (one per swapchain image) ---
    std::vector<VkImage>        depthImages = {};
    std::vector<VkDeviceMemory> depthMems   = {};
    std::vector<VkImageView>    depthViews  = {};

    VkCommandPool cmdPool = VK_NULL_HANDLE;

    std::vector<ViewportFrame> frames     = {};
    uint32_t                   frameIndex = 0;

    bool  needsRecreate    = false;
    QSize pendingPixelSize = {};

    std::vector<VkFramebuffer> framebuffers = {};

    DeferredDeletion deferred = {}; // per-viewport deferred destruction
};

struct ViewportFrameContext
{
    ViewportFrame* frame            = nullptr; // points into sc->frames[fi]
    uint32_t       imageIndex       = 0;       // acquired swapchain image
    uint32_t       frameIndex       = 0;       // fi (frame-in-flight ring index)
    bool           frameFenceWaited = false;
};

class VulkanBackend final
{
public:
    VulkanBackend() noexcept  = default;
    ~VulkanBackend() noexcept = default;

    VulkanBackend(const VulkanBackend&)            = delete;
    VulkanBackend(VulkanBackend&&)                 = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    VulkanBackend& operator=(VulkanBackend&&)      = delete;

    bool init(QVulkanInstance* qvk, uint32_t framesInFlight = 2);
    void shutdown() noexcept;

    ViewportSwapchain* createViewportSwapchain(QWindow* window);
    void               destroyViewportSwapchain(ViewportSwapchain* sc) noexcept;

    void resizeViewportSwapchain(ViewportSwapchain* sc, const QSize& newPixelSize) noexcept;

    bool beginFrame(ViewportSwapchain* sc, ViewportFrameContext& out) noexcept;
    void endFrame(ViewportSwapchain* sc, const ViewportFrameContext& fc) noexcept;

    void cancelFrame(ViewportSwapchain*          sc,
                     const ViewportFrameContext& fc,
                     bool                        clear,
                     float                       r = 0.0f,
                     float                       g = 0.0f,
                     float                       b = 0.0f,
                     float                       a = 1.0f) noexcept;

    void renderClear(ViewportSwapchain* sc, float r, float g, float b, float a);

    QVulkanInstance* qvk() const noexcept
    {
        return m_qvk;
    }

    VkDevice device() const noexcept
    {
        return m_device;
    }

    // ------------------------------------------------------------
    // VulkanContext
    // ------------------------------------------------------------
    VulkanContext& context() noexcept
    {
        return m_ctx;
    }
    const VulkanContext& context() const noexcept
    {
        return m_ctx;
    }

    QVulkanDeviceFunctions* deviceFunctions() const noexcept
    {
        return (m_qvk && m_device) ? m_qvk->deviceFunctions(m_device) : nullptr;
    }

private:
    bool createDevice();
    void ensureContext() noexcept;
    bool loadKhrEntryPoints() noexcept;

    bool createSwapchain(ViewportSwapchain* sc, const QSize& pixelSize);
    void destroySwapchainObjects(ViewportSwapchain* sc) noexcept;

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const noexcept;
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const noexcept;
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps, const QSize& pixelSize) const noexcept;

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const noexcept;

private:
    QVulkanInstance* m_qvk = nullptr;

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;

    uint32_t m_graphicsFamily = 0;
    uint32_t m_presentFamily  = 0;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue  = VK_NULL_HANDLE;

    uint32_t m_framesInFlight = 2;

    std::vector<ViewportSwapchain*> m_swapchains = {};

    VkSampleCountFlagBits m_sampleCount = VK_SAMPLE_COUNT_1_BIT;

    VkPhysicalDeviceProperties m_deviceProps = {};

private:
    // ------------------------------------------------------------
    // Cached VulkanContext (kept up-to-date by init/createDevice/loadKhrEntryPoints/shutdown)
    // ------------------------------------------------------------
    VulkanContext    m_ctx              = {};
    DeferredDeletion m_deferredDeletion = {};

private:
    // -------- core loader --------
    PFN_vkGetDeviceProcAddr m_vkGetDeviceProcAddr = nullptr;

    // -------- KHR instance functions --------
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR      m_vkGetPhysicalDeviceSurfaceSupportKHR      = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR      m_vkGetPhysicalDeviceSurfaceFormatsKHR      = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR m_vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;

    // -------- KHR device functions --------
    PFN_vkCreateSwapchainKHR    m_vkCreateSwapchainKHR    = nullptr;
    PFN_vkDestroySwapchainKHR   m_vkDestroySwapchainKHR   = nullptr;
    PFN_vkGetSwapchainImagesKHR m_vkGetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR   m_vkAcquireNextImageKHR   = nullptr;
    PFN_vkQueuePresentKHR       m_vkQueuePresentKHR       = nullptr;

    // ------------------------------------------------------------
    // Optional RT capability (device-level only)
    // ------------------------------------------------------------
    bool m_supportsRayTracing = false;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR    m_rtProps = {};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProps = {};

    // -------- optional RT device functions --------
    PFN_vkGetBufferDeviceAddressKHR m_vkGetBufferDeviceAddressKHR = nullptr;

    PFN_vkCreateAccelerationStructureKHR           m_vkCreateAccelerationStructureKHR           = nullptr;
    PFN_vkDestroyAccelerationStructureKHR          m_vkDestroyAccelerationStructureKHR          = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR    m_vkGetAccelerationStructureBuildSizesKHR    = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR        m_vkCmdBuildAccelerationStructuresKHR        = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_vkGetAccelerationStructureDeviceAddressKHR = nullptr;

    PFN_vkCreateRayTracingPipelinesKHR       m_vkCreateRayTracingPipelinesKHR       = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR m_vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR                    m_vkCmdTraceRaysKHR                    = nullptr;

    VulkanRtDispatch m_rtDispatch = {};
};
