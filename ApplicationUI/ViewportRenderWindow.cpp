//============================================================
// ViewportRenderWindow.cpp  (FULL REPLACEMENT)
//============================================================
#include "ViewportRenderWindow.hpp"

#include <qvulkanfunctions.h>

#include <Core.hpp>
#include <QEvent>
#include <QExposeEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <cmath>

#include "VulkanBackend.hpp"

namespace
{
    static QSize currentPixelSize(const QWindow* w)
    {
        const qreal dpr = w ? w->devicePixelRatio() : 1.0;
        return QSize(
            int(std::lround(double(w->width()) * double(dpr))),
            int(std::lround(double(w->height()) * double(dpr))));
    }
} // namespace

ViewportRenderWindow::ViewportRenderWindow(Core* core, Viewport* vp, VulkanBackend* backend) noexcept :
    m_core(core),
    m_viewport(vp),
    m_backend(backend)
{
    Q_ASSERT(m_core);
    Q_ASSERT(m_viewport);
    Q_ASSERT(m_backend);

    setSurfaceType(QSurface::VulkanSurface);

    // Optional: can reduce flashes in some setups.
    setFlag(Qt::FramelessWindowHint, true);

    // Continuous move tick (~60Hz)
    m_moveTimer.setInterval(16);
    m_moveTimer.setSingleShot(false);

    QObject::connect(&m_moveTimer, &QTimer::timeout, [this]() {
        tickMove();
    });
}

ViewportRenderWindow::~ViewportRenderWindow()
{
    // Make destruction idempotent and safe.
    destroySwapchain();

    m_backend  = nullptr;
    m_viewport = nullptr;
    m_core     = nullptr;
}

void ViewportRenderWindow::requestUpdateOnce() noexcept
{
    // Avoid UpdateRequest spam; queue at most one until it is processed.
    if (m_updateQueued)
        return;

    m_updateQueued = true;
    requestUpdate();
}

bool ViewportRenderWindow::event(QEvent* e)
{
    if (!e)
        return QWindow::event(e);

    // ------------------------------------------------------------
    // CRITICAL: destroy swapchain BEFORE Qt destroys the surface
    // ------------------------------------------------------------
    if (e->type() == QEvent::PlatformSurface)
    {
        auto* pe = static_cast<QPlatformSurfaceEvent*>(e);
        if (pe->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        {
            // Stop rendering loop and destroy swapchain now.
            m_exposed      = false;
            m_updateQueued = false;

            // Stop keyboard movement as well.
            m_move = {};
            if (m_moveTimer.isActive())
                m_moveTimer.stop();

            destroySwapchain();
        }
    }

    if (e->type() == QEvent::UpdateRequest)
    {
        // We consumed the queued update.
        m_updateQueued = false;

        renderOnce();

        return true;
    }

    return QWindow::event(e);
}

void ViewportRenderWindow::exposeEvent(QExposeEvent* e)
{
    QWindow::exposeEvent(e);

    m_exposed = isExposed();

    if (!m_exposed)
        return;

    ensureSwapchain();

    // Helpful: when a viewport appears, make it eligible to receive keys
    // (focus is still typically set on click, but this helps in some setups).
    requestActivate();

    requestUpdateOnce();
}

void ViewportRenderWindow::resizeEvent(QResizeEvent* e)
{
    QWindow::resizeEvent(e);

    if (!m_backend)
        return;

    const QSize px = currentPixelSize(this);

    // Core viewport uses pixel size.
    if (m_core && m_viewport)
        m_core->resizeViewport(m_viewport, px.width(), px.height());

    // Swapchain will be recreated lazily inside renderClear().
    if (m_swapchain)
        m_backend->resizeViewportSwapchain(m_swapchain, px);

    if (m_exposed)
        requestUpdateOnce();
}

void ViewportRenderWindow::focusOutEvent(QFocusEvent* e)
{
    // Prevent stuck keys if focus changes while a key is held.
    m_move = {};
    stopMoveTimerIfIdle();
    QWindow::focusOutEvent(e);
}

void ViewportRenderWindow::ensureSwapchain() noexcept
{
    if (!m_backend || m_swapchain)
        return;

    QVulkanInstance* qvk = m_backend->qvk();
    if (!qvk)
        return;

    // Must set Vulkan instance before surfaceForWindow().
    if (vulkanInstance() != qvk)
        setVulkanInstance(qvk);

    // Ensure native handle exists (important on Windows sometimes).
    if (!handle())
        create();

    const QSize px = currentPixelSize(this);
    if (px.width() <= 0 || px.height() <= 0)
        return;

    m_swapchain = m_backend->createViewportSwapchain(this);

    // ------------------------------------------------------------
    // Step 4B: initialize Core swapchain-dependent resources once
    // ------------------------------------------------------------
    if (m_swapchain && !m_coreSwapchainInited && m_core && m_swapchain->renderPass)
    {
        m_core->initializeSwapchain(m_swapchain->renderPass);
        m_coreSwapchainInited = true;
    }
}

void ViewportRenderWindow::destroySwapchain() noexcept
{
    // Idempotent.
    if (!m_swapchain)
        return;

    // If backend is already gone, we cannot destroy Vulkan objects safely here.
    // (But with your shutdown order, this shouldn't happen.)
    if (m_backend)
    {
        m_backend->destroyViewportSwapchain(m_swapchain);
    }

    m_swapchain = nullptr;
}

void ViewportRenderWindow::renderOnce() noexcept
{
    if (!m_exposed)
        return;

    ensureSwapchain();

    if (!m_backend || !m_swapchain || !m_core || !m_viewport)
        return;

    // If Core hasn't been initialized with a compatible render pass yet,
    // don't call Core::render(). (We can still clear.)
    if (!m_coreSwapchainInited)
    {
        m_backend->renderClear(m_swapchain, 0.032f, 0.049f, 0.074f, 1.0f);
        return;
    }

    QVulkanDeviceFunctions* df = m_backend->deviceFunctions();
    if (!df)
        return;

    ViewportFrameContext fc = {};
    if (!m_backend->beginFrame(m_swapchain, fc))
        return;

    // Build the per-call render context passed into CoreLib.
    RenderFrameContext rfc = {};
    rfc.cmd                = fc.frame->cmd;
    rfc.frameIndex         = fc.frameIndex;
    rfc.deferred           = &m_swapchain->deferred;
    rfc.frameFenceWaited   = fc.frameFenceWaited;

    // RT / compute / prepass work must happen outside render pass
    m_core->renderPrePass(m_viewport, rfc);

    VkClearValue clears[3] = {};
    clears[0].color        = {{0.032f, 0.049f, 0.074f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    clears[2].color        = {{0.0f, 0.0f, 0.0f, 0.0f}}; // resolve (unused)

    VkRenderPassBeginInfo rp = {};
    rp.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass            = m_swapchain->renderPass;
    rp.framebuffer           = m_swapchain->framebuffers[fc.imageIndex];
    rp.renderArea.offset     = {0, 0};
    rp.renderArea.extent     = m_swapchain->extent;
    rp.clearValueCount       = 3;
    rp.pClearValues          = clears;

    df->vkCmdBeginRenderPass(fc.frame->cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Draw scene via CoreLib
    m_core->render(m_viewport, rfc);

    df->vkCmdEndRenderPass(fc.frame->cmd);

    m_backend->endFrame(m_swapchain, fc);
}

CoreEvent ViewportRenderWindow::createCoreEvent(const QMouseEvent* e) const noexcept
{
    CoreEvent ev = {};

    const float dpr = static_cast<float>(devicePixelRatio());

    ev.button = static_cast<int>(e->button());
    ev.x      = static_cast<float>(e->position().x()) * dpr;
    ev.y      = static_cast<float>(e->position().y()) * dpr;

    ev.key_code  = 0;
    ev.shift_key = (e->modifiers() & Qt::ShiftModifier) != 0;
    ev.ctrl_key  = (e->modifiers() & Qt::ControlModifier) != 0;
    ev.cmd_key   = (e->modifiers() & Qt::MetaModifier) != 0;
    ev.alt_key   = (e->modifiers() & Qt::AltModifier) != 0;
    ev.dbl_click = e->type() == QEvent::MouseButtonDblClick;

    return ev;
}

void ViewportRenderWindow::mousePressEvent(QMouseEvent* e)
{
    if (!m_core || !m_viewport)
        return;

    // Ensure this window can receive keyboard input after a click.
    requestActivate();
    // setFocus(Qt::MouseFocusReason);

    m_lastPos = e->position();
    m_core->setActiveViewport(m_viewport);
    m_core->mousePressEvent(m_viewport, createCoreEvent(e));
    requestUpdateOnce();
}

void ViewportRenderWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_core || !m_viewport)
        return;

    CoreEvent ev = createCoreEvent(e);

    const float dpr = static_cast<float>(devicePixelRatio());
    ev.deltaX       = (e->position().x() - m_lastPos.x()) * dpr;
    ev.deltaY       = (e->position().y() - m_lastPos.y()) * dpr;

    m_lastPos = e->position();

    if (e->buttons() & Qt::LeftButton)
        m_core->mouseDragEvent(m_viewport, ev);
    else
        m_core->mouseMoveEvent(m_viewport, ev);
}

void ViewportRenderWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_core || !m_viewport)
        return;

    m_core->mouseReleaseEvent(m_viewport, createCoreEvent(e));
}

void ViewportRenderWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    const CoreEvent ev = createCoreEvent(e);
    m_core->mousePressEvent(m_viewport, ev);
}

void ViewportRenderWindow::wheelEvent(QWheelEvent* e)
{
    if (!m_core || !m_viewport)
        return;

    CoreEvent ev = {};
    ev.deltaY    = e->angleDelta().y() / 120.0f;

    m_core->mouseWheelEvent(m_viewport, ev);
}

//============================================================
// Continuous keyboard movement helpers
//============================================================
void ViewportRenderWindow::startMoveTimer() noexcept
{
    if (!m_moveClock.isValid())
        m_moveClock.start();
    else
        m_moveClock.restart();

    if (!m_moveTimer.isActive())
        m_moveTimer.start();
}

void ViewportRenderWindow::stopMoveTimerIfIdle() noexcept
{
    if (m_move.left || m_move.right || m_move.forward || m_move.backward)
        return;

    if (m_moveTimer.isActive())
        m_moveTimer.stop();
}

void ViewportRenderWindow::tickMove() noexcept
{
    if (!m_core || !m_viewport)
        return;

    // Drive shared camera from this window's viewport as the reference.
    m_core->setActiveViewport(m_viewport);

    const float dt = static_cast<float>(m_moveClock.restart()) / 1000.0f;
    if (dt <= 0.0f)
        return;

    const Qt::KeyboardModifiers mods  = QGuiApplication::keyboardModifiers();
    const bool                  alt   = (mods & Qt::AltModifier) != 0;
    const bool                  shift = (mods & Qt::ShiftModifier) != 0;

    // ------------------------------------------------------------
    // ALT held: rotate (yaw / pitch)
    // ------------------------------------------------------------
    if (alt)
    {
        const float rotSpeedPerSec = 60.0f;

        float rotX = 0.0f; // yaw
        float rotY = 0.0f; // pitch

        if (m_move.left)
            rotX += rotSpeedPerSec * dt;
        if (m_move.right)
            rotX -= rotSpeedPerSec * dt;

        if (m_move.forward) // Up
            rotY += rotSpeedPerSec * dt;
        if (m_move.backward) // Down
            rotY -= rotSpeedPerSec * dt;

        if (rotX != 0.0f || rotY != 0.0f)
            m_core->viewportRotate(m_viewport, rotX, rotY);

        return;
    }

    // ------------------------------------------------------------
    // SHIFT held (no ALT): vertical camera move (up / down)
    // ------------------------------------------------------------
    if (shift)
    {
        const float panSpeedPxPerSec = 400.0f;

        float panY = 0.0f;

        if (m_move.forward) // Shift + Up
            panY += panSpeedPxPerSec * dt;
        if (m_move.backward) // Shift + Down
            panY -= panSpeedPxPerSec * dt;

        if (panY != 0.0f)
            m_core->viewportPan(m_viewport, 0.0f, panY);

        return;
    }

    // ------------------------------------------------------------
    // No modifiers: strafe + dolly
    // ------------------------------------------------------------
    const float panSpeedPxPerSec = 400.0f;
    const float zoomUnitsPerSec  = 500.0f;

    float panX  = 0.0f;
    float zoomX = 0.0f;

    if (m_move.left)
        panX -= panSpeedPxPerSec * dt;
    if (m_move.right)
        panX += panSpeedPxPerSec * dt;

    if (m_move.forward) // Up
        zoomX += zoomUnitsPerSec * dt;
    if (m_move.backward) // Down
        zoomX -= zoomUnitsPerSec * dt;

    if (panX != 0.0f)
        m_core->viewportPan(m_viewport, panX, 0.0f);

    if (zoomX != 0.0f)
        m_core->viewportZoom(m_viewport, zoomX, 0.0f);
}

//============================================================
// Key events
//============================================================
void ViewportRenderWindow::keyPressEvent(QKeyEvent* e)
{
    if (!e || !m_core || !m_viewport)
        return;

    switch (e->key())
    {
        case Qt::Key_Left:
            m_move.left = true;
            break;

        case Qt::Key_Right:
            m_move.right = true;
            break;

        case Qt::Key_Up:
            m_move.forward = true;
            break;

        case Qt::Key_Down:
            m_move.backward = true;
            break;

        default:
            e->ignore();
            return;
    }

    m_core->setActiveViewport(m_viewport);
    startMoveTimer();

    e->accept();
}

void ViewportRenderWindow::keyReleaseEvent(QKeyEvent* e)
{
    if (!e)
        return;

    switch (e->key())
    {
        case Qt::Key_Left:
            m_move.left = false;
            break;

        case Qt::Key_Right:
            m_move.right = false;
            break;

        case Qt::Key_Up:
            m_move.forward = false;
            break;

        case Qt::Key_Down:
            m_move.backward = false;
            break;

        default:
            e->ignore();
            return;
    }

    stopMoveTimerIfIdle();

    e->accept();
}
