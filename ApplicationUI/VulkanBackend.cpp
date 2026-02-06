// ============================================================
// VulkanBackend.cpp
// ============================================================

#include "VulkanBackend.hpp"

#include <QMessageBox>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

#include "VkDebugNames.hpp"

namespace
{
    static QSize currentPixelSize(const QWindow* w)
    {
        const qreal dpr = w ? w->devicePixelRatio() : 1.0;
        return QSize(
            int(std::lround(double(w->width()) * double(dpr))),
            int(std::lround(double(w->height()) * double(dpr))));
    }

    static QVulkanFunctions* instFns(QVulkanInstance* qvk) noexcept
    {
        return qvk ? qvk->functions() : nullptr;
    }

    static QVulkanDeviceFunctions* devFns(QVulkanInstance* qvk, VkDevice dev) noexcept
    {
        return (qvk && dev) ? qvk->deviceFunctions(dev) : nullptr;
    }

    static VkSampleCountFlagBits getMaxUsableSampleCount(QVulkanFunctions* f, VkPhysicalDevice phys) noexcept
    {
        if (!f || !phys)
            return VK_SAMPLE_COUNT_1_BIT;

        VkPhysicalDeviceProperties props{};
        f->vkGetPhysicalDeviceProperties(phys, &props);

        const VkSampleCountFlags counts =
            props.limits.framebufferColorSampleCounts &
            props.limits.framebufferDepthSampleCounts;

        if (counts & VK_SAMPLE_COUNT_64_BIT)
            return VK_SAMPLE_COUNT_64_BIT;
        if (counts & VK_SAMPLE_COUNT_32_BIT)
            return VK_SAMPLE_COUNT_32_BIT;
        if (counts & VK_SAMPLE_COUNT_16_BIT)
            return VK_SAMPLE_COUNT_16_BIT;
        if (counts & VK_SAMPLE_COUNT_8_BIT)
            return VK_SAMPLE_COUNT_8_BIT;
        if (counts & VK_SAMPLE_COUNT_4_BIT)
            return VK_SAMPLE_COUNT_4_BIT;
        if (counts & VK_SAMPLE_COUNT_2_BIT)
            return VK_SAMPLE_COUNT_2_BIT;

        return VK_SAMPLE_COUNT_1_BIT;
    }

} // namespace

// ------------------------------------------------------------
// KHR loader (Qt 6.8 compatible)
// ------------------------------------------------------------
bool VulkanBackend::loadKhrEntryPoints() noexcept
{
    if (!m_qvk || !m_device)
        return false;

    auto loadInstLocal = [this](const char* name) noexcept -> PFN_vkVoidFunction {
        if (!m_qvk || !name)
            return nullptr;
        return reinterpret_cast<PFN_vkVoidFunction>(m_qvk->getInstanceProcAddr(name));
    };

    // Core loader (device proc addr)
    m_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(loadInstLocal("vkGetDeviceProcAddr"));
    if (!m_vkGetDeviceProcAddr)
        return false;

    vkutil::init(m_vkGetDeviceProcAddr, m_device); // just for debug validation names

    // -------- instance-level KHR (surface) --------
    m_vkGetPhysicalDeviceSurfaceSupportKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(loadInstLocal("vkGetPhysicalDeviceSurfaceSupportKHR"));
    m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(loadInstLocal("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    m_vkGetPhysicalDeviceSurfaceFormatsKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(loadInstLocal("vkGetPhysicalDeviceSurfaceFormatsKHR"));
    m_vkGetPhysicalDeviceSurfacePresentModesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(loadInstLocal("vkGetPhysicalDeviceSurfacePresentModesKHR"));

    if (!m_vkGetPhysicalDeviceSurfaceSupportKHR ||
        !m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR ||
        !m_vkGetPhysicalDeviceSurfaceFormatsKHR ||
        !m_vkGetPhysicalDeviceSurfacePresentModesKHR)
    {
        return false;
    }

    // -------- device-level loader --------
    auto loadDevLocal = [this](const char* name) noexcept -> PFN_vkVoidFunction {
        if (!m_vkGetDeviceProcAddr || !m_device || !name)
            return nullptr;
        return m_vkGetDeviceProcAddr(m_device, name);
    };

    // -------- device-level KHR (swapchain) --------
    m_vkCreateSwapchainKHR =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(loadDevLocal("vkCreateSwapchainKHR"));
    m_vkDestroySwapchainKHR =
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(loadDevLocal("vkDestroySwapchainKHR"));
    m_vkGetSwapchainImagesKHR =
        reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(loadDevLocal("vkGetSwapchainImagesKHR"));
    m_vkAcquireNextImageKHR =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(loadDevLocal("vkAcquireNextImageKHR"));
    m_vkQueuePresentKHR =
        reinterpret_cast<PFN_vkQueuePresentKHR>(loadDevLocal("vkQueuePresentKHR"));

    if (!m_vkCreateSwapchainKHR ||
        !m_vkDestroySwapchainKHR ||
        !m_vkGetSwapchainImagesKHR ||
        !m_vkAcquireNextImageKHR ||
        !m_vkQueuePresentKHR)
    {
        return false;
    }

    // ------------------------------------------------------------
    // Optional RT entry points (only if RT was enabled in createDevice)
    // ------------------------------------------------------------
    // Reset first (safe if re-init)
    m_vkGetBufferDeviceAddressKHR                = nullptr;
    m_vkCreateAccelerationStructureKHR           = nullptr;
    m_vkDestroyAccelerationStructureKHR          = nullptr;
    m_vkGetAccelerationStructureBuildSizesKHR    = nullptr;
    m_vkCmdBuildAccelerationStructuresKHR        = nullptr;
    m_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    m_vkCreateRayTracingPipelinesKHR             = nullptr;
    m_vkGetRayTracingShaderGroupHandlesKHR       = nullptr;
    m_vkCmdTraceRaysKHR                          = nullptr;

    if (m_supportsRayTracing)
    {
        m_vkGetBufferDeviceAddressKHR =
            reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(loadDevLocal("vkGetBufferDeviceAddressKHR"));

        m_vkCreateAccelerationStructureKHR =
            reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(loadDevLocal("vkCreateAccelerationStructureKHR"));
        m_vkDestroyAccelerationStructureKHR =
            reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(loadDevLocal("vkDestroyAccelerationStructureKHR"));
        m_vkGetAccelerationStructureBuildSizesKHR =
            reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(loadDevLocal("vkGetAccelerationStructureBuildSizesKHR"));
        m_vkCmdBuildAccelerationStructuresKHR =
            reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(loadDevLocal("vkCmdBuildAccelerationStructuresKHR"));
        m_vkGetAccelerationStructureDeviceAddressKHR =
            reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(loadDevLocal("vkGetAccelerationStructureDeviceAddressKHR"));

        m_vkCreateRayTracingPipelinesKHR =
            reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(loadDevLocal("vkCreateRayTracingPipelinesKHR"));
        m_vkGetRayTracingShaderGroupHandlesKHR =
            reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(loadDevLocal("vkGetRayTracingShaderGroupHandlesKHR"));
        m_vkCmdTraceRaysKHR =
            reinterpret_cast<PFN_vkCmdTraceRaysKHR>(loadDevLocal("vkCmdTraceRaysKHR"));

        // If any critical function is missing, disable RT support (raster still works).
        const bool ok =
            (m_vkGetBufferDeviceAddressKHR != nullptr) &&
            (m_vkCreateAccelerationStructureKHR != nullptr) &&
            (m_vkDestroyAccelerationStructureKHR != nullptr) &&
            (m_vkGetAccelerationStructureBuildSizesKHR != nullptr) &&
            (m_vkCmdBuildAccelerationStructuresKHR != nullptr) &&
            (m_vkGetAccelerationStructureDeviceAddressKHR != nullptr) &&
            (m_vkCreateRayTracingPipelinesKHR != nullptr) &&
            (m_vkGetRayTracingShaderGroupHandlesKHR != nullptr) &&
            (m_vkCmdTraceRaysKHR != nullptr);

        if (!ok)
        {
            std::cerr << "VulkanBackend: RT extensions enabled but RT entry points missing; disabling RT.\n";
            m_supportsRayTracing = false;
            m_rtProps            = {};
            m_asProps            = {};
        }

        if (m_supportsRayTracing)
        {
            m_rtDispatch                             = {};
            m_rtDispatch.vkGetBufferDeviceAddressKHR = m_vkGetBufferDeviceAddressKHR;

            m_rtDispatch.vkCreateAccelerationStructureKHR           = m_vkCreateAccelerationStructureKHR;
            m_rtDispatch.vkDestroyAccelerationStructureKHR          = m_vkDestroyAccelerationStructureKHR;
            m_rtDispatch.vkGetAccelerationStructureBuildSizesKHR    = m_vkGetAccelerationStructureBuildSizesKHR;
            m_rtDispatch.vkCmdBuildAccelerationStructuresKHR        = m_vkCmdBuildAccelerationStructuresKHR;
            m_rtDispatch.vkGetAccelerationStructureDeviceAddressKHR = m_vkGetAccelerationStructureDeviceAddressKHR;

            m_rtDispatch.vkCreateRayTracingPipelinesKHR       = m_vkCreateRayTracingPipelinesKHR;
            m_rtDispatch.vkGetRayTracingShaderGroupHandlesKHR = m_vkGetRayTracingShaderGroupHandlesKHR;
            m_rtDispatch.vkCmdTraceRaysKHR                    = m_vkCmdTraceRaysKHR;
        }
        else
        {
            m_rtDispatch = {};
        }
    }

    return true;
}

bool VulkanBackend::init(QVulkanInstance* qvk, uint32_t framesInFlight)
{
    if (!qvk)
        return false;

    m_qvk            = qvk;
    m_instance       = qvk->vkInstance();
    m_framesInFlight = std::clamp(framesInFlight, 1u, vkcfg::kMaxFramesInFlight);

    if (!instFns(m_qvk))
        return false;

    if (!createDevice())
        return false;

    if (!loadKhrEntryPoints())
        return false;

    ensureContext();
    return true;
}

void VulkanBackend::shutdown() noexcept
{
    // IMPORTANT: We must destroy swapchains BEFORE destroying the device.
    // Also we must destroy the device BEFORE the QVulkanInstance / VkInstance goes away.

    if (!m_device)
    {
        // Still clear bookkeeping.
        m_swapchains.clear();
        m_qvk      = nullptr;
        m_instance = VK_NULL_HANDLE;
        m_ctx      = {};
        vkutil::shutdown();
        return;
    }

    // If Qt has already destroyed the QVulkanInstance, we cannot safely call QVulkanDeviceFunctions.
    // So this shutdown MUST be called while m_qvk is still alive.
    if (!m_qvk)
        return;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (df)
        df->vkDeviceWaitIdle(m_device);

    // Destroy any remaining swapchains we created (windows may not have cleaned up in time).
    // Copy the list because destroyViewportSwapchain will remove from m_swapchains.
    const auto swapchainsCopy = m_swapchains;
    for (ViewportSwapchain* sc : swapchainsCopy)
    {
        if (sc)
            destroyViewportSwapchain(sc);
    }
    m_swapchains.clear();

    // Now destroy the device.
    if (df && m_device)
        df->vkDestroyDevice(m_device, nullptr);

    m_device         = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_graphicsQueue  = VK_NULL_HANDLE;
    m_presentQueue   = VK_NULL_HANDLE;

    // Clear loaded pointers
    m_vkGetDeviceProcAddr                       = nullptr;
    m_vkGetPhysicalDeviceSurfaceSupportKHR      = nullptr;
    m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    m_vkGetPhysicalDeviceSurfaceFormatsKHR      = nullptr;
    m_vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
    m_vkCreateSwapchainKHR                      = nullptr;
    m_vkDestroySwapchainKHR                     = nullptr;
    m_vkGetSwapchainImagesKHR                   = nullptr;
    m_vkAcquireNextImageKHR                     = nullptr;
    m_vkQueuePresentKHR                         = nullptr;

    // Ray tracing related
    m_vkGetBufferDeviceAddressKHR                = nullptr;
    m_vkCreateAccelerationStructureKHR           = nullptr;
    m_vkDestroyAccelerationStructureKHR          = nullptr;
    m_vkGetAccelerationStructureBuildSizesKHR    = nullptr;
    m_vkCmdBuildAccelerationStructuresKHR        = nullptr;
    m_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    m_vkCreateRayTracingPipelinesKHR             = nullptr;
    m_vkGetRayTracingShaderGroupHandlesKHR       = nullptr;
    m_vkCmdTraceRaysKHR                          = nullptr;
    m_rtDispatch                                 = {};
    m_supportsRayTracing                         = false;
    m_rtProps                                    = {};
    m_asProps                                    = {};

    m_qvk      = nullptr;
    m_instance = VK_NULL_HANDLE;

    m_ctx = {};

    // Note: m_deferredDeletion is owned here; nothing special required.
    vkutil::shutdown(); // just for debug validation names
}

bool VulkanBackend::createDevice()
{
    QVulkanFunctions* f = instFns(m_qvk);
    if (!f)
        return false;

    // ------------------------------------------------------------
    // Enumerate physical devices
    // ------------------------------------------------------------
    uint32_t devCount = 0;
    f->vkEnumeratePhysicalDevices(m_instance, &devCount, nullptr);
    if (devCount == 0)
        return false;

    std::vector<VkPhysicalDevice> devices(devCount);
    f->vkEnumeratePhysicalDevices(m_instance, &devCount, devices.data());

    // ------------------------------------------------------------
    // Local helpers (kept inside the function for true drop-in)
    // ------------------------------------------------------------
    auto versionStr = [](uint32_t v) -> std::string {
        return std::to_string(VK_VERSION_MAJOR(v)) + "." +
               std::to_string(VK_VERSION_MINOR(v)) + "." +
               std::to_string(VK_VERSION_PATCH(v));
    };

    auto deviceTypeStr = [](VkPhysicalDeviceType t) noexcept -> const char* {
        switch (t)
        {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                return "Discrete";
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                return "Integrated";
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                return "Virtual";
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                return "CPU";
            default:
                return "Other";
        }
    };

    auto vendorStr = [](uint32_t vendorId) noexcept -> const char* {
        switch (vendorId)
        {
            case 0x10DE:
                return "NVIDIA";
            case 0x1002:
                return "AMD";
            case 0x8086:
                return "Intel";
            case 0x13B5:
                return "ARM";
            case 0x5143:
                return "Qualcomm";
            default:
                return "Unknown";
        }
    };

    auto queryInstanceVulkanVersion = [&](QVulkanInstance* qvk) noexcept -> uint32_t {
        if (!qvk)
            return VK_API_VERSION_1_0;

        auto fp = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            qvk->getInstanceProcAddr("vkEnumerateInstanceVersion"));

        if (!fp)
            return VK_API_VERSION_1_0;

        uint32_t v = VK_API_VERSION_1_0;
        if (fp(&v) != VK_SUCCESS)
            return VK_API_VERSION_1_0;

        return v;
    };

    auto enumerateDeviceExts = [&](VkPhysicalDevice pd, std::vector<VkExtensionProperties>& outExts) -> void {
        uint32_t extCount = 0;
        f->vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);

        outExts.clear();
        outExts.resize(extCount);

        if (extCount)
            f->vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, outExts.data());
    };

    auto hasExtInList = [&](const std::vector<VkExtensionProperties>& exts, const char* name) noexcept -> bool {
        if (!name)
            return false;

        for (const auto& e : exts)
        {
            if (std::strcmp(e.extensionName, name) == 0)
                return true;
        }
        return false;
    };

    auto findGraphicsFamily = [&](VkPhysicalDevice pd, uint32_t& outFamily) noexcept -> bool {
        outFamily = 0xFFFFFFFFu;

        uint32_t familyCount = 0;
        f->vkGetPhysicalDeviceQueueFamilyProperties(pd, &familyCount, nullptr);
        if (familyCount == 0)
            return false;

        std::vector<VkQueueFamilyProperties> qprops(familyCount);
        f->vkGetPhysicalDeviceQueueFamilyProperties(pd, &familyCount, qprops.data());

        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                outFamily = i;
                return true;
            }
        }
        return false;
    };

    auto scoreDevice = [&](VkPhysicalDevice                    pd,
                           VkPhysicalDeviceProperties&         outProps,
                           VkPhysicalDeviceFeatures&           outFeats,
                           VkPhysicalDeviceFeatures2&          outSupportedCore,
                           std::vector<VkExtensionProperties>& outExts,
                           uint32_t&                           outGraphicsFamily,
                           bool&                               outMeetsHardReqs) -> int {
        outMeetsHardReqs  = false;
        outGraphicsFamily = 0xFFFFFFFFu;

        f->vkGetPhysicalDeviceProperties(pd, &outProps);
        f->vkGetPhysicalDeviceFeatures(pd, &outFeats);

        outSupportedCore       = {};
        outSupportedCore.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f->vkGetPhysicalDeviceFeatures2(pd, &outSupportedCore);

        enumerateDeviceExts(pd, outExts);

        // Hard requirement: graphics queue
        if (!findGraphicsFamily(pd, outGraphicsFamily))
            return -1;

        // Hard requirement: swapchain extension (I always enable it)
        if (!hasExtInList(outExts, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            return -1;

        // Hard requirement: we unconditionally enable these features later
        if (!outFeats.geometryShader || !outFeats.samplerAnisotropy)
            return -1;

        outMeetsHardReqs = true;

        int score = 0;

        // Prefer discrete
        switch (outProps.deviceType)
        {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                score += 1000;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                score += 300;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                score += 150;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                score += 10;
                break;
            default:
                score += 50;
                break;
        }

        // Prefer higher Vulkan API version
        score += int(VK_VERSION_MAJOR(outProps.apiVersion)) * 100;
        score += int(VK_VERSION_MINOR(outProps.apiVersion)) * 10;

        // Prefer more MSAA capability (rough heuristic)
        const VkSampleCountFlags msaa =
            outProps.limits.framebufferColorSampleCounts &
            outProps.limits.framebufferDepthSampleCounts;

        if (msaa & VK_SAMPLE_COUNT_64_BIT)
            score += 60;
        else if (msaa & VK_SAMPLE_COUNT_32_BIT)
            score += 50;
        else if (msaa & VK_SAMPLE_COUNT_16_BIT)
            score += 40;
        else if (msaa & VK_SAMPLE_COUNT_8_BIT)
            score += 30;
        else if (msaa & VK_SAMPLE_COUNT_4_BIT)
            score += 20;
        else if (msaa & VK_SAMPLE_COUNT_2_BIT)
            score += 10;

        // Prefer bigger texture limits (very rough signal)
        score += int(outProps.limits.maxImageDimension2D / 1024);

        return score;
    };

    // ------------------------------------------------------------
    // Candidate selection
    // ------------------------------------------------------------
    struct Candidate
    {
        VkPhysicalDevice                   pd = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties         props{};
        VkPhysicalDeviceFeatures           feats{};
        VkPhysicalDeviceFeatures2          supportedCore{};
        std::vector<VkExtensionProperties> exts;
        uint32_t                           graphicsFamily = 0xFFFFFFFFu;
        int                                score          = -1;
        bool                               meets          = false;
    };

    // Instance Vulkan version (loader/runtime)
    m_instanceVulkanVersion = queryInstanceVulkanVersion(m_qvk);

    std::vector<Candidate> cands;
    cands.reserve(devices.size());

    // std::cerr << "VulkanBackend: Enumerating physical devices (" << devices.size() << "):\n";

    Candidate best     = {};
    bool      haveBest = false;

    for (VkPhysicalDevice pd : devices)
    {
        Candidate c = {};
        c.pd        = pd;

        c.score = scoreDevice(pd, c.props, c.feats, c.supportedCore, c.exts, c.graphicsFamily, c.meets);

        // std::cerr
        //     << "  - " << c.props.deviceName
        //     << " | " << vendorStr(c.props.vendorID) << " (0x" << std::hex << c.props.vendorID << std::dec << ")"
        //     << " | " << deviceTypeStr(c.props.deviceType)
        //     << " | Vulkan " << versionStr(c.props.apiVersion)
        //     << " | Driver " << versionStr(c.props.driverVersion)
        //     << "\n";

        // std::cerr
        //     << "      features: geometryShader=" << (c.feats.geometryShader ? "YES" : "no")
        //     << " samplerAnisotropy=" << (c.feats.samplerAnisotropy ? "YES" : "no")
        //     << " shaderInt64=" << (c.supportedCore.features.shaderInt64 ? "YES" : "no")
        //     << "\n";

        // std::cerr
        //     << "      ext: VK_KHR_swapchain=" << (hasExtInList(c.exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ? "YES" : "no")
        //     << "\n";

        // if (c.score >= 0)
        //     std::cerr << "      score: " << c.score << "\n";
        // else
        //     std::cerr << "      score: (rejected)\n";

        cands.push_back(c);

        if (c.meets && c.score >= 0)
        {
            if (!haveBest || c.score > best.score)
            {
                best     = c;
                haveBest = true;
            }
        }
    }

    if (!haveBest || !best.pd)
    {
        // Show a friendly UI error in the UI layer and fail init gracefully.
        // NOTE: requires #include <QMessageBox> and #include <sstream> at top of VulkanBackend.cpp
        uint32_t maxDevVulkan = VK_API_VERSION_1_0;

        std::ostringstream oss;
        oss << "IMP3D could not find a Vulkan device suitable for rendering.\n\n";
        oss << "Vulkan loader (instance) version: " << versionStr(m_instanceVulkanVersion) << "\n\n";
        oss << "Detected GPUs:\n";

        for (const auto& c : cands)
        {
            maxDevVulkan = std::max(maxDevVulkan, c.props.apiVersion);

            const bool hasSwap = hasExtInList(c.exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            const bool hasGfx  = (c.graphicsFamily != 0xFFFFFFFFu);

            oss << "  - " << c.props.deviceName
                << " (" << deviceTypeStr(c.props.deviceType) << ")"
                << " Vulkan " << versionStr(c.props.apiVersion)
                << " Swapchain=" << (hasSwap ? "YES" : "no")
                << " GraphicsQueue=" << (hasGfx ? "YES" : "no")
                << " Aniso=" << (c.feats.samplerAnisotropy ? "YES" : "no")
                << " GeomShader=" << (c.feats.geometryShader ? "YES" : "no")
                << "\n";
        }

        oss << "\nMax Vulkan supported by any detected GPU: " << versionStr(maxDevVulkan) << "\n";

        QMessageBox::critical(nullptr,
                              "Vulkan device not supported",
                              QString::fromStdString(oss.str()));

        return false;
    }

    // ------------------------------------------------------------
    // Selected physical device
    // ------------------------------------------------------------
    m_physicalDevice = best.pd;
    m_deviceProps    = best.props;

    // Queue family
    m_graphicsFamily = best.graphicsFamily;

    // Minimal assumption: present = graphics. We validate per-surface on swapchain creation.
    m_presentFamily = m_graphicsFamily;

    std::cerr
        << "VulkanBackend: Selected device: " << m_deviceProps.deviceName
        << " (" << deviceTypeStr(m_deviceProps.deviceType) << "), Vulkan "
        << versionStr(m_deviceProps.apiVersion) << "\n";

    // Query max MSAA from HW (selected device)
    m_sampleCount = getMaxUsableSampleCount(f, m_physicalDevice);

    const float prio = 1.0f;

    VkDeviceQueueCreateInfo qci = {};
    qci.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex        = m_graphicsFamily;
    qci.queueCount              = 1;
    qci.pQueuePriorities        = &prio;

    // ------------------------------------------------------------
    // Extensions available on the selected device
    // ------------------------------------------------------------
    const std::vector<VkExtensionProperties>& availExts = best.exts;

    auto hasExt = [&](const char* name) noexcept -> bool {
        return hasExtInList(availExts, name);
    };

    const uint32_t apiMajor = VK_VERSION_MAJOR(m_deviceProps.apiVersion);
    const uint32_t apiMinor = VK_VERSION_MINOR(m_deviceProps.apiVersion);
    const bool     apiAtLeast12 =
        (apiMajor > 1) || (apiMajor == 1 && apiMinor >= 2);

    // ------------------------------------------------------------
    // Query core features (ALWAYS) - we need shaderInt64 for RT shaders
    // ------------------------------------------------------------
    VkPhysicalDeviceFeatures2 supportedCore = best.supportedCore;

    // Base extensions (always)
    std::vector<const char*> enabledExts;
    enabledExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // ------------------------------------------------------------
    // Timeline semaphore support (Vulkan 1.2 core OR KHR extension)
    // ------------------------------------------------------------
    const bool hasTimelineExt    = hasExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    const bool timelineAvailable = apiAtLeast12 || hasTimelineExt;

    // We only need to enable the extension if the device is < 1.2
    const bool needTimelineExtEnable = (!apiAtLeast12) && hasTimelineExt;

    // Query timeline feature support (via Features2 chain)
    VkPhysicalDeviceTimelineSemaphoreFeatures supportedTimeline = {};
    supportedTimeline.sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

    VkPhysicalDeviceFeatures2 supportedTimelineQuery = {};
    supportedTimelineQuery.sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedTimelineQuery.pNext                     = &supportedTimeline;

    if (timelineAvailable)
        f->vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supportedTimelineQuery);

    const bool timelineOk =
        timelineAvailable && (supportedTimeline.timelineSemaphore == VK_TRUE);

    if (needTimelineExtEnable)
        enabledExts.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

    // ------------------------------------------------------------
    // Optional ray tracing extension bundle
    // ------------------------------------------------------------
    const bool rtExtsOk =
        hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        hasExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
        hasExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
        hasExt(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
        hasExt(VK_KHR_SPIRV_1_4_EXTENSION_NAME) &&
        hasExt(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

    m_supportsRayTracing = rtExtsOk;

    // RT shaders in my pipeline currently use 64-bit addresses => SPIR-V Int64 capability.
    // If the device doesn't support shaderInt64, disable RT.
    if (m_supportsRayTracing)
    {
        if (!supportedCore.features.shaderInt64)
        {
            std::cerr << "VulkanBackend: shaderInt64 not supported; disabling ray tracing.\n";
            m_supportsRayTracing = false;
        }
    }

    // ------------------------------------------------------------
    // Query feature support for RT (only if extensions exist and shaderInt64 is supported)
    // ------------------------------------------------------------
    VkPhysicalDeviceFeatures2 supported = {};
    supported.sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceBufferDeviceAddressFeatures supportedBda = {};
    supportedBda.sType                                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAs = {};
    supportedAs.sType                                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRt = {};
    supportedRt.sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    if (m_supportsRayTracing)
    {
        // Chain for query
        supported.pNext    = &supportedBda;
        supportedBda.pNext = &supportedAs;
        supportedAs.pNext  = &supportedRt;
        supportedRt.pNext  = nullptr;

        f->vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported);

        if (!supportedBda.bufferDeviceAddress ||
            !supportedAs.accelerationStructure ||
            !supportedRt.rayTracingPipeline)
        {
            m_supportsRayTracing = false;
        }
    }

    // If still supported, enable the RT extension set
    if (m_supportsRayTracing)
    {
        enabledExts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        enabledExts.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        enabledExts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        enabledExts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        enabledExts.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        enabledExts.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    }
    else
    {
        std::cerr << "VulkanBackend: Ray tracing not available; RT draw mode will be disabled.\n";
    }

    // ------------------------------------------------------------
    // Features (use Features2 so we can optionally chain RT features)
    // ------------------------------------------------------------
    VkPhysicalDeviceFeatures2 feats2 = {};
    feats2.sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    feats2.features.geometryShader    = VK_TRUE;
    feats2.features.samplerAnisotropy = VK_TRUE;

    // Enable shaderInt64 if supported (required by RT shaders).
    feats2.features.shaderInt64 = supportedCore.features.shaderInt64 ? VK_TRUE : VK_FALSE;

    // Optional RT feature chain (only used if m_supportsRayTracing == true)
    VkPhysicalDeviceBufferDeviceAddressFeatures bda = {};
    bda.sType                                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeat = {};
    asFeat.sType                                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeat = {};
    rtFeat.sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    // Timeline semaphore feature (enable if supported)
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeat = {};
    timelineFeat.sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

    void* pNextChain = nullptr;

    auto chain = [&](auto& s) {
        s.pNext    = pNextChain;
        pNextChain = &s;
    };

    if (m_supportsRayTracing)
    {
        // Enable minimal set for first RT pass
        bda.bufferDeviceAddress      = VK_TRUE;
        asFeat.accelerationStructure = VK_TRUE;
        rtFeat.rayTracingPipeline    = VK_TRUE;

        chain(rtFeat);
        chain(asFeat);
        chain(bda);
    }

    if (timelineOk)
    {
        timelineFeat.timelineSemaphore = VK_TRUE;
        chain(timelineFeat);
    }

    feats2.pNext = pNextChain;

    // ------------------------------------------------------------
    // Create device (Features2 is passed via pNext)
    // ------------------------------------------------------------
    VkDeviceCreateInfo dci      = {};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &feats2;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = uint32_t(enabledExts.size());
    dci.ppEnabledExtensionNames = enabledExts.data();
    dci.pEnabledFeatures        = nullptr; // MUST be null when using Features2

    if (f->vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS)
        return false;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (!df)
        return false;

    df->vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    df->vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);

    // ------------------------------------------------------------
    // Query RT properties (SBT alignments, recursion limits, etc.)
    // ------------------------------------------------------------
    m_rtProps = {};
    m_asProps = {};

    if (m_supportsRayTracing)
    {
        m_rtProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        m_asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

        // Chain properties query
        props2.pNext    = &m_rtProps;
        m_rtProps.pNext = &m_asProps;
        m_asProps.pNext = nullptr;

        f->vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
    }

    if (timelineOk)
        std::cerr << "VulkanBackend: Timeline semaphores enabled.\n";
    else
        std::cerr << "VulkanBackend: Timeline semaphores not available (will use fences).\n";

    if (m_supportsRayTracing)
        std::cerr << "VulkanBackend: Ray tracing enabled.\n";
    else
        std::cerr << "VulkanBackend: Ray tracing not available (running raster-only).\n";

    return true;
}

// ============================================================
// VulkanBackend.cpp (add ensureContext(), remove fillContext usage)
// ============================================================

void VulkanBackend::ensureContext() noexcept
{
    m_ctx                          = {};
    m_ctx.instance                 = m_instance;
    m_ctx.physicalDevice           = m_physicalDevice;
    m_ctx.device                   = m_device;
    m_ctx.graphicsQueue            = m_graphicsQueue;
    m_ctx.graphicsQueueFamilyIndex = m_graphicsFamily;
    m_ctx.framesInFlight           = m_framesInFlight;
    m_ctx.sampleCount              = m_sampleCount;
    m_ctx.deviceProps              = m_deviceProps;
    m_ctx.supportsRayTracing       = m_supportsRayTracing;
    m_ctx.rtProps                  = m_rtProps;
    m_ctx.asProps                  = m_asProps;
    m_ctx.rtDispatch               = m_supportsRayTracing ? &m_rtDispatch : nullptr;
    m_ctx.allocator                = nullptr;
}

ViewportSwapchain* VulkanBackend::createViewportSwapchain(QWindow* window)
{
    if (!m_qvk || !m_device || !window)
        return nullptr;

    if (!m_vkGetPhysicalDeviceSurfaceSupportKHR)
        return nullptr;

    auto* sc   = new ViewportSwapchain;
    *sc        = {};
    sc->window = window;

    sc->surface = m_qvk->surfaceForWindow(window);
    if (!sc->surface)
    {
        delete sc;
        return nullptr;
    }

    VkBool32 presentOk = VK_FALSE;
    m_vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_graphicsFamily, sc->surface, &presentOk);
    if (!presentOk)
    {
        std::cerr << "VulkanBackend: graphics queue family does not support present on this surface.\n";
        delete sc;
        return nullptr;
    }

    const QSize px = currentPixelSize(window);
    if (!createSwapchain(sc, px))
    {
        destroyViewportSwapchain(sc);
        return nullptr;
    }

    // Track it so shutdown() can clean up even if windows die out-of-order.
    m_swapchains.push_back(sc);

    return sc;
}

void VulkanBackend::destroyViewportSwapchain(ViewportSwapchain* sc) noexcept
{
    if (!sc)
        return;

    // Remove from tracking list first (idempotent).
    if (!m_swapchains.empty())
    {
        auto it = std::find(m_swapchains.begin(), m_swapchains.end(), sc);
        if (it != m_swapchains.end())
            m_swapchains.erase(it);
    }

    if (!m_qvk || !m_device)
    {
        // If we cannot safely destroy Vulkan objects (Qt instance already gone),
        // we must not call into Vulkan here. But ideally shutdown order prevents this.
        delete sc;
        return;
    }

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (df && m_device)
        df->vkDeviceWaitIdle(m_device);

    destroySwapchainObjects(sc);

    // Surface is managed by Qt when created via surfaceForWindow().
    delete sc;
}

void VulkanBackend::resizeViewportSwapchain(ViewportSwapchain* sc, const QSize& newPixelSize) noexcept
{
    if (!sc)
        return;

    if (newPixelSize.width() <= 0 || newPixelSize.height() <= 0)
        return;

    sc->pendingPixelSize = newPixelSize;
    sc->needsRecreate    = true;
}

VkSurfaceFormatKHR VulkanBackend::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const noexcept
{
    for (const auto& f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }

    if (!formats.empty())
        return formats[0];

    VkSurfaceFormatKHR fallback = {};
    fallback.format             = VK_FORMAT_B8G8R8A8_UNORM;
    fallback.colorSpace         = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    return fallback;
}

VkPresentModeKHR VulkanBackend::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const noexcept
{
    for (auto m : modes)
    {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR; // always available
}

VkExtent2D VulkanBackend::chooseExtent(const VkSurfaceCapabilitiesKHR& caps, const QSize& pixelSize) const noexcept
{
    if (caps.currentExtent.width != 0xFFFFFFFFu)
        return caps.currentExtent;

    VkExtent2D e = {};
    e.width      = std::clamp<uint32_t>(uint32_t(pixelSize.width()), caps.minImageExtent.width, caps.maxImageExtent.width);
    e.height     = std::clamp<uint32_t>(uint32_t(pixelSize.height()), caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

uint32_t VulkanBackend::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const noexcept
{
    QVulkanFunctions* f = instFns(m_qvk);
    if (!f)
        return 0;

    VkPhysicalDeviceMemoryProperties mp = {};
    f->vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return 0;
}

bool VulkanBackend::createSwapchain(ViewportSwapchain* sc, const QSize& pixelSize)
{
    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);

    if (!sc || !df || !m_device)
        return false;

    if (!m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR ||
        !m_vkGetPhysicalDeviceSurfaceFormatsKHR ||
        !m_vkGetPhysicalDeviceSurfacePresentModesKHR ||
        !m_vkCreateSwapchainKHR ||
        !m_vkGetSwapchainImagesKHR)
    {
        return false;
    }

    // If caller is recreating, it should have already destroyed old objects.
    // We'll still be defensive and wipe anything that might be dangling.
    destroySwapchainObjects(sc);

    // ------------------------------------------------------------
    // Helpers (local)
    // ------------------------------------------------------------
    auto createImage2D = [&](VkFormat              format,
                             VkImageUsageFlags     usage,
                             VkImageAspectFlags    aspect,
                             VkSampleCountFlagBits samples,
                             VkImage&              outImage,
                             VkDeviceMemory&       outMem,
                             VkImageView&          outView) -> bool {
        outImage = VK_NULL_HANDLE;
        outMem   = VK_NULL_HANDLE;
        outView  = VK_NULL_HANDLE;

        VkImageCreateInfo ici = {};
        ici.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType         = VK_IMAGE_TYPE_2D;
        ici.format            = format;
        ici.extent            = {sc->extent.width, sc->extent.height, 1};
        ici.mipLevels         = 1;
        ici.arrayLayers       = 1;
        ici.samples           = samples;
        ici.tiling            = VK_IMAGE_TILING_OPTIMAL;
        ici.usage             = usage;
        ici.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

        if (df->vkCreateImage(m_device, &ici, nullptr, &outImage) != VK_SUCCESS)
            return false;

        VkMemoryRequirements mr = {};
        df->vkGetImageMemoryRequirements(m_device, outImage, &mr);

        VkMemoryAllocateInfo mai = {};
        mai.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize       = mr.size;
        mai.memoryTypeIndex      = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (mai.memoryTypeIndex == 0 && (mr.memoryTypeBits & 1u) == 0)
            return false;

        if (df->vkAllocateMemory(m_device, &mai, nullptr, &outMem) != VK_SUCCESS)
            return false;

        if (df->vkBindImageMemory(m_device, outImage, outMem, 0) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo vci           = {};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = outImage;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = format;
        vci.subresourceRange.aspectMask     = aspect;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;

        if (df->vkCreateImageView(m_device, &vci, nullptr, &outView) != VK_SUCCESS)
            return false;

        return true;
    };

    auto createSwapchainImageView = [&](VkImage image, VkFormat format, VkImageView& outView) -> bool {
        outView = VK_NULL_HANDLE;

        VkImageViewCreateInfo vci           = {};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = format;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;

        return df->vkCreateImageView(m_device, &vci, nullptr, &outView) == VK_SUCCESS;
    };

    // ------------------------------------------------------------
    // Choose surface details
    // ------------------------------------------------------------
    // Store what this swapchain was created with.
    sc->sampleCount = m_sampleCount;

    VkSurfaceCapabilitiesKHR caps = {};
    m_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, sc->surface, &caps);

    uint32_t fmtCount = 0;
    m_vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, sc->surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    if (fmtCount)
        m_vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, sc->surface, &fmtCount, formats.data());

    uint32_t pmCount = 0;
    m_vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, sc->surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> modes(pmCount);
    if (pmCount)
        m_vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, sc->surface, &pmCount, modes.data());

    const VkSurfaceFormatKHR sf = chooseSurfaceFormat(formats);
    const VkPresentModeKHR   pm = choosePresentMode(modes);
    const VkExtent2D         ex = chooseExtent(caps, pixelSize);

    if (ex.width == 0 || ex.height == 0)
        return false;

    uint32_t imageCount = std::max(caps.minImageCount + 1u, 2u);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    // ------------------------------------------------------------
    // Create swapchain
    // ------------------------------------------------------------
    VkSwapchainCreateInfoKHR ci = {};
    ci.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface                  = sc->surface;
    ci.minImageCount            = imageCount;
    ci.imageFormat              = sf.format;
    ci.imageColorSpace          = sf.colorSpace;
    ci.imageExtent              = ex;
    ci.imageArrayLayers         = 1;
    ci.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform             = caps.currentTransform;
    ci.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode              = pm;
    ci.clipped                  = VK_TRUE;
    ci.oldSwapchain             = VK_NULL_HANDLE;

    if (m_vkCreateSwapchainKHR(m_device, &ci, nullptr, &sc->swapchain) != VK_SUCCESS)
        return false;

    vkutil::name(m_device, sc->swapchain, "Viewport.Swapchain");

    sc->colorFormat = sf.format;
    sc->extent      = ex;

    // ------------------------------------------------------------
    // Get swapchain images + create views
    // ------------------------------------------------------------
    uint32_t imgCount = 0;
    m_vkGetSwapchainImagesKHR(m_device, sc->swapchain, &imgCount, nullptr);
    if (imgCount == 0)
    {
        destroySwapchainObjects(sc);
        return false;
    }

    sc->images.resize(imgCount);
    m_vkGetSwapchainImagesKHR(m_device, sc->swapchain, &imgCount, sc->images.data());

    sc->views.resize(imgCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < imgCount; ++i)
    {
        if (!createSwapchainImageView(sc->images[i], sc->colorFormat, sc->views[i]))
        {
            destroySwapchainObjects(sc);
            return false;
        }

        // Note: swapchain VkImage objects are not "owned" images we created,
        // so many tools won't show names for them; naming their views is enough.
        vkutil::name(m_device, sc->views[i], "Viewport.SwapchainView", int32_t(i));
    }

    // ------------------------------------------------------------
    // Render pass (MSAA color + MSAA depth + resolve)
    // ------------------------------------------------------------
    VkAttachmentDescription attachments[3] = {};

    // 0) MSAA color
    attachments[0].format         = sc->colorFormat;
    attachments[0].samples        = sc->sampleCount;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 1) MSAA depth
    constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
    attachments[1].format           = kDepthFormat;
    attachments[1].samples          = sc->sampleCount;
    attachments[1].loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp   = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout    = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 2) Resolve (swapchain)
    attachments[2].format         = sc->colorFormat;
    attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {};
    colorRef.attachment            = 0;
    colorRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef = {};
    depthRef.attachment            = 1;
    depthRef.layout                = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolveRef = {};
    resolveRef.attachment            = 2;
    resolveRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub    = {};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pResolveAttachments     = &resolveRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpci = {};
    rpci.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount        = 3;
    rpci.pAttachments           = attachments;
    rpci.subpassCount           = 1;
    rpci.pSubpasses             = &sub;

    if (df->vkCreateRenderPass(m_device, &rpci, nullptr, &sc->renderPass) != VK_SUCCESS)
    {
        destroySwapchainObjects(sc);
        return false;
    }

    vkutil::name(m_device, sc->renderPass, "Viewport.RenderPass");

    // ------------------------------------------------------------
    // Per-swapchain-image MSAA color + depth
    // ------------------------------------------------------------
    sc->msaaColorImages.resize(imgCount, VK_NULL_HANDLE);
    sc->msaaColorMems.resize(imgCount, VK_NULL_HANDLE);
    sc->msaaColorViews.resize(imgCount, VK_NULL_HANDLE);

    sc->depthImages.resize(imgCount, VK_NULL_HANDLE);
    sc->depthMems.resize(imgCount, VK_NULL_HANDLE);
    sc->depthViews.resize(imgCount, VK_NULL_HANDLE);

    for (uint32_t i = 0; i < imgCount; ++i)
    {
        if (!createImage2D(sc->colorFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT,
                           sc->sampleCount,
                           sc->msaaColorImages[i],
                           sc->msaaColorMems[i],
                           sc->msaaColorViews[i]))
        {
            destroySwapchainObjects(sc);
            return false;
        }

        vkutil::name(m_device, sc->msaaColorImages[i], "Viewport.MsaaColorImage", int32_t(i));
        vkutil::name(m_device, sc->msaaColorViews[i], "Viewport.MsaaColorView", int32_t(i));
        vkutil::name(m_device, sc->msaaColorMems[i], "Viewport.MsaaColorMem", int32_t(i));

        if (!createImage2D(kDepthFormat,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT,
                           sc->sampleCount,
                           sc->depthImages[i],
                           sc->depthMems[i],
                           sc->depthViews[i]))
        {
            destroySwapchainObjects(sc);
            return false;
        }

        vkutil::name(m_device, sc->depthImages[i], "Viewport.DepthImage", int32_t(i));
        vkutil::name(m_device, sc->depthViews[i], "Viewport.DepthView", int32_t(i));
        vkutil::name(m_device, sc->depthMems[i], "Viewport.DepthMem", int32_t(i));
    }

    // ------------------------------------------------------------
    // Command pool
    // ------------------------------------------------------------
    VkCommandPoolCreateInfo cpci = {};
    cpci.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex        = m_graphicsFamily;
    cpci.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (df->vkCreateCommandPool(m_device, &cpci, nullptr, &sc->cmdPool) != VK_SUCCESS)
    {
        destroySwapchainObjects(sc);
        return false;
    }

    vkutil::name(m_device, sc->cmdPool, "Viewport.CmdPool");

    // ------------------------------------------------------------
    // Frames in flight (cmd + sync)
    // ------------------------------------------------------------
    sc->frames.resize(m_framesInFlight);

    sc->deferred.init(uint32_t(sc->frames.size()));

    // NOTE: Deferred deletion is now global (m_deferredDeletion) and keyed by Viewport*.
    // VulkanBackend does NOT flush here because it doesn't know the Viewport* key.

    std::vector<VkCommandBuffer> cmds(m_framesInFlight, VK_NULL_HANDLE);

    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool                 = sc->cmdPool;
    cbai.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount          = m_framesInFlight;

    if (df->vkAllocateCommandBuffers(m_device, &cbai, cmds.data()) != VK_SUCCESS)
    {
        destroySwapchainObjects(sc);
        return false;
    }

    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        sc->frames[i].cmd = cmds[i];
        vkutil::name(m_device, sc->frames[i].cmd, "Viewport.Cmd", int32_t(i));

        VkFenceCreateInfo fci = {};
        fci.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

        if (df->vkCreateFence(m_device, &fci, nullptr, &sc->frames[i].fence) != VK_SUCCESS)
        {
            destroySwapchainObjects(sc);
            return false;
        }

        vkutil::name(m_device, sc->frames[i].fence, "Viewport.Fence", int32_t(i));

        VkSemaphoreCreateInfo sci = {};
        sci.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (df->vkCreateSemaphore(m_device, &sci, nullptr, &sc->frames[i].imageAvailable) != VK_SUCCESS)
        {
            destroySwapchainObjects(sc);
            return false;
        }

        vkutil::name(m_device, sc->frames[i].imageAvailable, "Viewport.SemImageAvailable", int32_t(i));

        if (df->vkCreateSemaphore(m_device, &sci, nullptr, &sc->frames[i].renderFinished) != VK_SUCCESS)
        {
            destroySwapchainObjects(sc);
            return false;
        }

        vkutil::name(m_device, sc->frames[i].renderFinished, "Viewport.SemRenderFinished", int32_t(i));
    }

    // ------------------------------------------------------------
    // Framebuffers (one per swapchain image)
    // Attachments: [msaaColor, msaaDepth, resolveSwapchain]
    // ------------------------------------------------------------
    sc->framebuffers.resize(imgCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < imgCount; ++i)
    {
        VkImageView atts[3] =
            {
                sc->msaaColorViews[i],
                sc->depthViews[i],
                sc->views[i]};

        VkFramebufferCreateInfo fci = {};
        fci.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass              = sc->renderPass;
        fci.attachmentCount         = 3;
        fci.pAttachments            = atts;
        fci.width                   = sc->extent.width;
        fci.height                  = sc->extent.height;
        fci.layers                  = 1;

        if (df->vkCreateFramebuffer(m_device, &fci, nullptr, &sc->framebuffers[i]) != VK_SUCCESS)
        {
            destroySwapchainObjects(sc);
            return false;
        }

        vkutil::name(m_device, sc->framebuffers[i], "Viewport.Framebuffer", int32_t(i));
    }

    sc->needsRecreate    = false;
    sc->pendingPixelSize = QSize();

    return true;
}

void VulkanBackend::destroySwapchainObjects(ViewportSwapchain* sc) noexcept
{
    if (!sc || !m_qvk || !m_device)
        return;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (!df)
        return;

    // ------------------------------------------------------------
    // Framebuffers (depend on render pass + image views)
    // ------------------------------------------------------------
    for (VkFramebuffer fb : sc->framebuffers)
    {
        if (fb)
            df->vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    sc->framebuffers.clear();

    // ------------------------------------------------------------
    // Command pool (implicitly frees command buffers allocated from it)
    // ------------------------------------------------------------
    if (sc->cmdPool)
    {
        df->vkDestroyCommandPool(m_device, sc->cmdPool, nullptr);
        sc->cmdPool = VK_NULL_HANDLE;
    }

    // ------------------------------------------------------------
    // Per-frame sync objects
    // ------------------------------------------------------------
    for (auto& fr : sc->frames)
    {
        if (fr.fence)
            df->vkDestroyFence(m_device, fr.fence, nullptr);

        if (fr.imageAvailable)
            df->vkDestroySemaphore(m_device, fr.imageAvailable, nullptr);

        if (fr.renderFinished)
            df->vkDestroySemaphore(m_device, fr.renderFinished, nullptr);

        fr = {};
    }
    sc->frames.clear();

    // ------------------------------------------------------------
    // MSAA color (one per swapchain image)
    // ------------------------------------------------------------
    for (VkImageView v : sc->msaaColorViews)
    {
        if (v)
            df->vkDestroyImageView(m_device, v, nullptr);
    }
    sc->msaaColorViews.clear();

    for (VkImage img : sc->msaaColorImages)
    {
        if (img)
            df->vkDestroyImage(m_device, img, nullptr);
    }
    sc->msaaColorImages.clear();

    for (VkDeviceMemory mem : sc->msaaColorMems)
    {
        if (mem)
            df->vkFreeMemory(m_device, mem, nullptr);
    }
    sc->msaaColorMems.clear();

    // ------------------------------------------------------------
    // Depth (one per swapchain image)
    // ------------------------------------------------------------
    for (VkImageView v : sc->depthViews)
    {
        if (v)
            df->vkDestroyImageView(m_device, v, nullptr);
    }
    sc->depthViews.clear();

    for (VkImage img : sc->depthImages)
    {
        if (img)
            df->vkDestroyImage(m_device, img, nullptr);
    }
    sc->depthImages.clear();

    for (VkDeviceMemory mem : sc->depthMems)
    {
        if (mem)
            df->vkFreeMemory(m_device, mem, nullptr);
    }
    sc->depthMems.clear();

    // ------------------------------------------------------------
    // Render pass
    // ------------------------------------------------------------
    if (sc->renderPass)
        df->vkDestroyRenderPass(m_device, sc->renderPass, nullptr);
    sc->renderPass = VK_NULL_HANDLE;

    // ------------------------------------------------------------
    // Swapchain image views
    // ------------------------------------------------------------
    for (VkImageView v : sc->views)
    {
        if (v)
            df->vkDestroyImageView(m_device, v, nullptr);
    }
    sc->views.clear();
    sc->images.clear();

    // ------------------------------------------------------------
    // Swapchain
    // ------------------------------------------------------------
    if (sc->swapchain && m_vkDestroySwapchainKHR)
        m_vkDestroySwapchainKHR(m_device, sc->swapchain, nullptr);
    sc->swapchain = VK_NULL_HANDLE;

    // Reset bookkeeping
    sc->frameIndex       = 0;
    sc->needsRecreate    = false;
    sc->pendingPixelSize = QSize();
    sc->extent           = {};
    sc->colorFormat      = VK_FORMAT_UNDEFINED;
    sc->sampleCount      = VK_SAMPLE_COUNT_1_BIT;
}

bool VulkanBackend::beginFrame(ViewportSwapchain* sc, ViewportFrameContext& out) noexcept
{
    if (!sc || !m_qvk || !m_device || !sc->swapchain)
        return false;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (!df)
        return false;

    if (!m_vkAcquireNextImageKHR)
        return false;

    // Handle resize/recreate requested externally
    if (sc->needsRecreate)
    {
        const QSize px = sc->pendingPixelSize.isValid()
                             ? sc->pendingPixelSize
                             : QSize(int(sc->extent.width), int(sc->extent.height));

        df->vkDeviceWaitIdle(m_device);
        destroySwapchainObjects(sc);

        if (!createSwapchain(sc, px))
            return false;

        sc->needsRecreate    = false;
        sc->pendingPixelSize = QSize();
    }

    if (sc->frames.empty())
        return false;

    const uint32_t fi = sc->frameIndex % m_framesInFlight;
    ViewportFrame& fr = sc->frames[fi];

    // Wait for this frame slot to be available.
    VkResult wr = df->vkWaitForFences(m_device, 1, &fr.fence, VK_TRUE, 1000000000ull);
    if (wr == VK_TIMEOUT)
    {
        std::cerr << "VulkanBackend: frame fence timeout (GPU hang). Forcing device idle + swapchain recreate.\n";
        df->vkDeviceWaitIdle(m_device);
        sc->needsRecreate = true;
        return false;
    }

    out.frameFenceWaited = true;
    // All work previously submitted for this frame slot is complete.
    // Now it's safe to destroy resources deferred for this slot (PER-VIEWPORT queue).
    sc->deferred.flush(fi);

    uint32_t imageIndex = 0;
    VkResult acq        = m_vkAcquireNextImageKHR(
        m_device,
        sc->swapchain,
        UINT64_MAX,
        fr.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);

    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        sc->needsRecreate = true;
        return false;
    }

    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        return false;

    if (acq == VK_SUBOPTIMAL_KHR)
        sc->needsRecreate = true;

    df->vkResetCommandBuffer(fr.cmd, 0);

    VkCommandBufferBeginInfo bi = {};
    bi.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (df->vkBeginCommandBuffer(fr.cmd, &bi) != VK_SUCCESS)
        return false;

    out.frame      = &fr;
    out.imageIndex = imageIndex;
    out.frameIndex = fi;
    return true;
}

void VulkanBackend::endFrame(ViewportSwapchain* sc, const ViewportFrameContext& fc) noexcept
{
    if (!sc || !m_qvk || !m_device || !sc->swapchain || !fc.frame)
        return;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (!df)
        return;

    if (!m_vkQueuePresentKHR)
        return;

    if (df->vkEndCommandBuffer(fc.frame->cmd) != VK_SUCCESS)
        return;

    // Reset the fence ONLY when we are actually going to submit work that will signal it.
    df->vkResetFences(m_device, 1, &fc.frame->fence);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si         = {};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &fc.frame->imageAvailable;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &fc.frame->cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &fc.frame->renderFinished;

    if (df->vkQueueSubmit(m_graphicsQueue, 1, &si, fc.frame->fence) != VK_SUCCESS)
        return;

    VkPresentInfoKHR pi   = {};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &fc.frame->renderFinished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc->swapchain;
    pi.pImageIndices      = &fc.imageIndex;

    VkResult pres = m_vkQueuePresentKHR(m_presentQueue, &pi);

    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR)
        sc->needsRecreate = true;

    sc->frameIndex = (sc->frameIndex + 1) % m_framesInFlight;
}

void VulkanBackend::cancelFrame(ViewportSwapchain*          sc,
                                const ViewportFrameContext& fc,
                                bool                        clear,
                                float                       r,
                                float                       g,
                                float                       b,
                                float                       a) noexcept
{
    if (!sc || !m_qvk || !m_device || !sc->swapchain || !fc.frame)
        return;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (!df)
        return;

    // We are inside an open command buffer (beginFrame() already vkBeginCommandBuffer'd it).
    // Optionally record a minimal render pass that clears attachments, so the presented image
    // is deterministic even when the main renderer bails.
    if (clear)
    {
        VkClearValue clears[3] = {};
        clears[0].color        = {{r, g, b, a}};
        clears[1].depthStencil = {1.0f, 0};
        clears[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};

        VkRenderPassBeginInfo rp = {};
        rp.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass            = sc->renderPass;
        rp.framebuffer           = sc->framebuffers[fc.imageIndex];
        rp.renderArea.offset     = {0, 0};
        rp.renderArea.extent     = sc->extent;
        rp.clearValueCount       = 3;
        rp.pClearValues          = clears;

        df->vkCmdBeginRenderPass(fc.frame->cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        df->vkCmdEndRenderPass(fc.frame->cmd);
    }

    // Always finalize submit + present.
    endFrame(sc, fc);
}

void VulkanBackend::renderClear(ViewportSwapchain* sc, float r, float g, float b, float a)
{
    if (!sc || !m_qvk || !m_device || !sc->swapchain)
        return;

    QVulkanDeviceFunctions* df = devFns(m_qvk, m_device);
    if (!df)
        return;

    ViewportFrameContext fc = {};
    if (!beginFrame(sc, fc))
        return;

    // 3 attachments in the MSAA render pass:
    //  0: MSAA color  (cleared)
    //  1: MSAA depth  (cleared)
    //  2: Resolve     (loadOp = DONT_CARE, so clear value is unused but harmless)
    VkClearValue clears[3] = {};
    clears[0].color        = {{r, g, b, a}};
    clears[1].depthStencil = {1.0f, 0};
    clears[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo rp = {};
    rp.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass            = sc->renderPass;
    rp.framebuffer           = sc->framebuffers[fc.imageIndex];
    rp.renderArea.offset     = {0, 0};
    rp.renderArea.extent     = sc->extent;
    rp.clearValueCount       = 3;
    rp.pClearValues          = clears;

    df->vkCmdBeginRenderPass(fc.frame->cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    df->vkCmdEndRenderPass(fc.frame->cmd);

    endFrame(sc, fc);
}
