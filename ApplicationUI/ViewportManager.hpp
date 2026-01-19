#ifndef VIEWPORTMANAGER_HPP
#define VIEWPORTMANAGER_HPP

#include <QSplitter>
#include <QWidget>
#include <vector>

#include "VulkanBackend.hpp"

class Core;
class QVulkanInstance;
class ViewportWidget;

/**
 * @brief Manages a 2x2 grid of Vulkan-backed viewports.
 *
 * ViewportManager owns and arranges four ViewportWidget instances in a
 * split-layout (top/bottom rows, left/right columns). It is responsible for:
 *
 * - Building and maintaining the splitter-based UI layout
 * - Coordinating viewport expand / restore (maximize one viewport)
 * - Driving per-frame rendering requests
 * - Owning and shutting down the shared Vulkan backend in a safe order
 *
 * This widget does not own Core, but relies on it for render state and
 * device-level Vulkan resources.
 */
class ViewportManager : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the viewport manager and initializes Vulkan.
     *
     * @param parent     Parent QWidget.
     * @param core       Application core (must outlive ViewportManager).
     * @param vkInstance Vulkan instance used to initialize the backend.
     *
     * Creates the VulkanBackend, initializes the device, builds the UI,
     * and creates four ViewportWidget instances.
     */
    explicit ViewportManager(QWidget* parent, Core* core, QVulkanInstance* vkInstance);

    /**
     * @brief Destructor.
     *
     * Ensures Vulkan resources are shut down in the correct order.
     * Child widgets are deleted automatically by Qt.
     */
    ~ViewportManager() override;

    /**
     * @brief Returns the managed viewport widgets.
     *
     * @return Constant reference to the internal viewport list.
     */
    const std::vector<ViewportWidget*>& viewports() const noexcept;

    /**
     * @brief Idle callback used to trigger rendering.
     *
     * Called by the application when idle; requests rendering on
     * visible viewports if the Core indicates rendering is needed.
     *
     * @param core Core instance driving the application.
     */
    void idleEvent(Core* core);

    /**
     * @brief Requests a render on all visible viewports.
     *
     * Viewports that are hidden or too small are skipped.
     */
    void renderViews();

    /**
     * @brief Shuts down Vulkan resources in a safe and deterministic order.
     *
     * This function is idempotent and can be called multiple times.
     * It ensures:
     *  - Per-viewport swapchains are destroyed first
     *  - Core device resources are released
     *  - The Vulkan device is destroyed last
     */
    void shutdownVulkan() noexcept;

protected:
    /**
     * @brief Qt show event override.
     *
     * Used to perform a one-time initial viewport expansion
     * after the widget is first shown.
     */
    void showEvent(QShowEvent* e) override;

private slots:
    /**
     * @brief Handles viewport expand / restore requests.
     *
     * When triggered by a ViewportWidget, this toggles between:
     *  - A 4-up layout where all viewports are visible
     *  - A maximized layout where only the sender viewport is visible
     *
     * Splitter rows are hidden as needed to avoid visual gaps or handles.
     *
     * @param sender The viewport requesting expansion.
     */
    void expandViewportAction(QWidget* sender);

    /**
     * @brief Synchronizes bottom splitter column sizes with the top splitter.
     *
     * Keeps left/right viewport widths aligned between rows.
     */
    void syncBottomSplitters(int pos, int index);

    /**
     * @brief Synchronizes top splitter column sizes with the bottom splitter.
     *
     * Keeps left/right viewport widths aligned between rows.
     */
    void syncTopSplitters(int pos, int index);

private:
    /**
     * @brief Builds the splitter-based UI and creates viewport widgets.
     *
     * Creates:
     *  - A vertical splitter for top/bottom rows
     *  - Horizontal splitters for left/right columns
     *  - Four ViewportWidget instances
     */
    void buildUi();

private:
    /// Vertical splitter dividing top and bottom viewport rows
    QSplitter* m_mainSplitter = nullptr;

    /// Horizontal splitter for the top row (2 viewports)
    QSplitter* m_topSplitter = nullptr;

    /// Horizontal splitter for the bottom row (2 viewports)
    QSplitter* m_botSplitter = nullptr;

    /// Shared Vulkan backend used by all viewports
    std::unique_ptr<VulkanBackend> m_backend;

    /// Managed viewport widgets (2x2 layout)
    std::vector<ViewportWidget*> m_viewports = {};

    /// Application core (not owned)
    Core* m_core = nullptr;

    /// Guards one-time initial expansion on first show
    bool m_didInitialExpand = false;
};

#endif // VIEWPORTMANAGER_HPP
