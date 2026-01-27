//============================================================
// ViewportRenderWindow.hpp  (FULL REPLACEMENT)
//============================================================
#pragma once

#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QWindow>

#include "CoreTypes.hpp"

class Core;
class Viewport;
class VulkanBackend;
struct ViewportSwapchain;

class ViewportRenderWindow final : public QWindow
{
    Q_OBJECT
public:
    explicit ViewportRenderWindow(Core* core, Viewport* vp, VulkanBackend* backend) noexcept;
    ~ViewportRenderWindow() override;

    Viewport* viewport() const noexcept
    {
        return m_viewport;
    }

    ViewportSwapchain* swapchain() const noexcept
    {
        return m_swapchain;
    }

    void requestUpdateOnce() noexcept;

protected:
    bool event(QEvent* e) override;

    void exposeEvent(QExposeEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

    void focusOutEvent(QFocusEvent* e) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void ensureSwapchain() noexcept;
    void destroySwapchain() noexcept;
    void renderOnce() noexcept;

    CoreEvent createCoreEvent(const QMouseEvent* e) const noexcept;

    // Continuous keyboard movement
    struct MoveKeys
    {
        bool left     = false;
        bool right    = false;
        bool forward  = false; // Up
        bool backward = false; // Down
    };

    void startMoveTimer() noexcept;
    void stopMoveTimerIfIdle() noexcept;
    void tickMove() noexcept;

private:
    Core*              m_core      = nullptr;
    Viewport*          m_viewport  = nullptr;
    VulkanBackend*     m_backend   = nullptr;
    ViewportSwapchain* m_swapchain = nullptr;

    QPointF m_lastPos             = {};
    bool    m_exposed             = false;
    bool    m_updateQueued        = false;
    bool    m_coreSwapchainInited = false;

    MoveKeys m_move = {};
    QTimer   m_moveTimer;
    //= {};
    QElapsedTimer m_moveClock = {};
};
