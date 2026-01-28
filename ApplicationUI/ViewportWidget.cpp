#include "ViewportWidget.hpp"

#include <Core.hpp>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

#include "ViewportRenderWindow.hpp"
#include "VulkanBackend.hpp"
#include "ui_ViewportWidget.h"

ViewportWidget::ViewportWidget(QWidget* parent, Core* core, VulkanBackend* backend) :
    QWidget(parent),
    m_core{core},
    m_backend{backend},
    ui(new Ui::ViewportWidget)
{
    ui->setupUi(this);

    // Disable Ray Trace draw mode if backend does not support it
    if (m_backend && !m_backend->supportsRayTracing())
    {
        const int rtIndex = static_cast<int>(DrawMode::RAY_TRACE);

        ui->cmbDrawType->setItemData(
            rtIndex,
            QVariant(0),
            Qt::UserRole - 1 // disables the item
        );

        ui->cmbDrawType->setItemData(
            rtIndex,
            "Ray tracing is not supported by the current GPU / driver.",
            Qt::ToolTipRole);

        // If Ray Trace was somehow selected, fall back to SHADED
        if (ui->cmbDrawType->currentIndex() == rtIndex)
            ui->cmbDrawType->setCurrentIndex(static_cast<int>(DrawMode::SHADED));
    }

    if (m_core)
    {
        m_viewport = m_core->createViewport();
        m_core->initializeViewport(m_viewport);
    }

    // Embed QWindow render surface
    if (m_backend && m_core && m_viewport)
    {
        m_window = new ViewportRenderWindow(m_core, m_viewport, m_backend);

        m_container = QWidget::createWindowContainer(m_window, ui->renderPlaceholder);
        m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // m_container->setFocusPolicy(Qt::StrongFocus);
        // ui->renderPlaceholder->setFocusProxy(m_container);

        QVBoxLayout* l = qobject_cast<QVBoxLayout*>(ui->renderPlaceholder->layout());
        if (!l)
        {
            l = new QVBoxLayout(ui->renderPlaceholder);
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(0);
        }
        l->addWidget(m_container);

        ui->renderPlaceholder->setAutoFillBackground(false);
        m_container->setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    connect(ui->btnMove, &ScrollButton::scrollButtonAction, this, &ViewportWidget::scrollButtonAction);
    connect(ui->btnZoom, &ScrollButton::scrollButtonAction, this, &ViewportWidget::scrollButtonAction);
    connect(ui->btnRotate, &ScrollButton::scrollButtonAction, this, &ViewportWidget::scrollButtonAction);

    connect(ui->cmbViewType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewportWidget::cmbViewTypeChanged);
    connect(ui->cmbDrawType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewportWidget::cmbDrawTypeChanged);

    connect(ui->btnExpand, &QPushButton::clicked, this, [this]() {
        emit expandViewportAction(this);
    });
}

ViewportWidget::~ViewportWidget()
{
    shutdownVulkan();
    delete ui;
}

void ViewportWidget::setInitialViewMode(ViewMode mode) noexcept
{
    QSignalBlocker blocker(ui->cmbViewType);
    ui->cmbViewType->setCurrentIndex(static_cast<int>(mode));
    m_core->viewMode(m_viewport, mode);
}

void ViewportWidget::setInitialDrawMode(DrawMode mode) noexcept
{
    QSignalBlocker blocker(ui->cmbDrawType);
    ui->cmbDrawType->setCurrentIndex(static_cast<int>(mode));
    m_core->drawMode(m_viewport, mode);
}

void ViewportWidget::shutdownVulkan() noexcept
{
    setUpdatesEnabled(false);

    // Detach focus proxy to avoid dangling focus targets.
    if (ui && ui->renderPlaceholder)
        ui->renderPlaceholder->setFocusProxy(nullptr);

    // Delete the container first (destroys QWindow -> destroys swapchain in its dtor).
    if (m_container)
    {
        delete m_container;
        m_container = nullptr;
    }

    m_window  = nullptr;
    m_backend = nullptr;
}

QWidget* ViewportWidget::renderHost() const
{
    return ui->renderPlaceholder;
}

void ViewportWidget::requestRender()
{
    // if (m_container)
    // m_container->setFocus(Qt::OtherFocusReason);
    if (m_window)
        m_window->requestUpdate();
}

void ViewportWidget::cmbViewTypeChanged(int index)
{
    if (!m_core || !m_viewport)
        return;

    m_core->viewMode(m_viewport, static_cast<ViewMode>(index));
}

void ViewportWidget::cmbDrawTypeChanged(int index)
{
    if (!m_core || !m_viewport)
        return;

    m_core->drawMode(m_viewport, static_cast<DrawMode>(index));
}

void ViewportWidget::scrollButtonAction(QWidget* sender, QPoint delta)
{
    if (!m_core || !m_viewport)
        return;

    const int dx = static_cast<int>(std::lround(delta.x()));
    const int dy = static_cast<int>(std::lround(delta.y()));

    if (sender == ui->btnMove)
        m_core->viewportPan(m_viewport, dx, dy);
    if (sender == ui->btnZoom)
        m_core->viewportZoom(m_viewport, dx, dy);
    if (sender == ui->btnRotate)
        m_core->viewportRotate(m_viewport, dx, dy);
}
