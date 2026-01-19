#include "ViewportManager.hpp"

#include <Core.hpp>
#include <QTimer>
#include <QVBoxLayout>

#include "ViewportWidget.hpp"
#include "VulkanBackend.hpp"

ViewportManager::ViewportManager(QWidget* parent, Core* core, QVulkanInstance* vkInstance) :
    QWidget(parent),
    m_core(core)
{
    Q_ASSERT(m_core);
    Q_ASSERT(vkInstance);

    m_backend = std::make_unique<VulkanBackend>();

    if (m_backend->init(vkInstance, 2))
    {
        m_core->initializeDevice(m_backend->context());
    }
    else
    {
        qFatal("Failed to init VulkanBackend");
    }

    buildUi();

    // Initial View and Draw modes for each viewport
    m_viewports[0]->setInitialViewMode(ViewMode::TOP);
    m_viewports[0]->setInitialDrawMode(DrawMode::WIREFRAME);

    m_viewports[1]->setInitialViewMode(ViewMode::PERSPECTIVE);
    m_viewports[1]->setInitialDrawMode(DrawMode::SOLID);

    m_viewports[2]->setInitialViewMode(ViewMode::FRONT);
    m_viewports[2]->setInitialDrawMode(DrawMode::WIREFRAME);

    m_viewports[3]->setInitialViewMode(ViewMode::LEFT);
    m_viewports[3]->setInitialDrawMode(DrawMode::WIREFRAME);
}

ViewportManager::~ViewportManager()
{
    shutdownVulkan();
    // Qt will delete child widgets (splitters, viewports, etc).
}

const std::vector<ViewportWidget*>& ViewportManager::viewports() const noexcept
{
    return m_viewports;
}

void ViewportManager::idleEvent(Core* core)
{
    if (m_core)
    {
        if (m_core->needsRender())
            renderViews();
    }
}

void ViewportManager::shutdownVulkan() noexcept
{
    // Idempotent
    if (!m_backend)
        return;

    // Stop any UI-driven update spam during teardown.
    setUpdatesEnabled(false);

    // Also stop updates inside each viewport widget (prevents QWidget paints / resize noise)
    for (ViewportWidget* vp : m_viewports)
    {
        if (vp)
            vp->setUpdatesEnabled(false);
    }

    // ------------------------------------------------------------
    // CRITICAL ORDER:
    // 1) Destroy per-viewport swapchains while surfaces still exist.
    // 2) Destroy Core Vulkan resources (descriptor pools, buffers, mapped memory, etc.)
    // 3) THEN destroy VkDevice (backend shutdown).
    // ------------------------------------------------------------

    // 1) Swapchains first
    for (ViewportWidget* vp : m_viewports)
    {
        if (vp)
            vp->shutdownVulkan(); // this must destroy the ViewportSwapchain + stop render loop
    }

    // 2) Core must release ALL device objects before VkDevice is destroyed
    if (m_core)
    {
        m_core->destroySwapchainResources(); // pipelines/renderpass-dependent resources
        m_core->destroy();                   // device-level resources: descriptor pools, buffers, etc.
    }

    // 3) Now it is safe to destroy the VkDevice
    m_backend->shutdown();
    m_backend.reset();
}

void ViewportManager::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);

    if (m_didInitialExpand)
        return;

    m_didInitialExpand = true;

    expandViewportAction(m_viewports[1]);
}

void ViewportManager::buildUi()
{
    // Splitters
    m_mainSplitter = new QSplitter(Qt::Vertical, this);
    m_topSplitter  = new QSplitter(Qt::Horizontal, m_mainSplitter);
    m_botSplitter  = new QSplitter(Qt::Horizontal, m_mainSplitter);

    m_mainSplitter->setHandleWidth(1);
    m_topSplitter->setHandleWidth(1);
    m_botSplitter->setHandleWidth(1);

    m_mainSplitter->setChildrenCollapsible(false);
    m_topSplitter->setChildrenCollapsible(false);
    m_botSplitter->setChildrenCollapsible(false);

    // Create 4 viewport widgets
    m_viewports.clear();
    m_viewports.reserve(4);

    for (int i = 0; i < 4; ++i)
    {
        // Parent = nullptr: splitter will own/reparent it on addWidget().
        ViewportWidget* vp = new ViewportWidget(nullptr, m_core, m_backend.get());

        connect(vp, &ViewportWidget::expandViewportAction, this, &ViewportManager::expandViewportAction);

        if (i < 2)
            m_topSplitter->addWidget(vp);
        else
            m_botSplitter->addWidget(vp);

        m_viewports.push_back(vp);
    }

    // Layout (ViewportManager owns the layout)
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_mainSplitter);
    setLayout(mainLayout);

    // Keep top/bottom splitters in sync horizontally
    connect(m_topSplitter, &QSplitter::splitterMoved, this, &ViewportManager::syncBottomSplitters);
    connect(m_botSplitter, &QSplitter::splitterMoved, this, &ViewportManager::syncTopSplitters);

    // Prefer stretch factors over arbitrary initial sizes
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 1);

    m_topSplitter->setStretchFactor(0, 1);
    m_topSplitter->setStretchFactor(1, 1);

    m_botSplitter->setStretchFactor(0, 1);
    m_botSplitter->setStretchFactor(1, 1);
}

void ViewportManager::renderViews()
{
    if (!m_backend)
        return;

    for (ViewportWidget* vpw : m_viewports)
    {
        if (!vpw || !vpw->isVisible() || vpw->width() <= 1 || vpw->height() <= 1)
            continue;

        vpw->requestRender();
    }
}

void ViewportManager::expandViewportAction(QWidget* sender)
{
    auto* vpw = qobject_cast<ViewportWidget*>(sender);
    if (!vpw)
        return;

    bool othersVisible = false;
    for (auto* vp : m_viewports)
    {
        if (vp && vp != vpw && vp->isVisible())
        {
            othersVisible = true;
            break;
        }
    }

    const bool maximize = othersVisible;

    for (auto* vp : m_viewports)
    {
        if (!vp)
            continue;
        vp->setVisible(maximize ? (vp == vpw) : true);
    }

    if (maximize)
    {
        const bool inTop = (vpw->parentWidget() == m_topSplitter);
        m_topSplitter->setVisible(true);
        m_botSplitter->setVisible(true);

        if (inTop)
            m_botSplitter->setVisible(false);
        else
            m_topSplitter->setVisible(false);
    }
    else
    {
        m_topSplitter->setVisible(true);
        m_botSplitter->setVisible(true);
    }
}

void ViewportManager::syncBottomSplitters(int /*pos*/, int /*index*/)
{
    m_botSplitter->setSizes(m_topSplitter->sizes());
}

void ViewportManager::syncTopSplitters(int /*pos*/, int /*index*/)
{
    m_topSplitter->setSizes(m_botSplitter->sizes());
}
