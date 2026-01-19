#pragma once

#include <QPointF>
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

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void ensureSwapchain() noexcept;
    void destroySwapchain() noexcept;
    void renderOnce() noexcept;

    CoreEvent createCoreEvent(const QMouseEvent* e) const noexcept;

private:
    Core*              m_core      = nullptr;
    Viewport*          m_viewport  = nullptr;
    VulkanBackend*     m_backend   = nullptr;
    ViewportSwapchain* m_swapchain = nullptr;

    QPointF m_lastPos      = {};
    bool    m_exposed      = false;
    bool    m_updateQueued = false;
    bool    m_coreSwapchainInited = false;
};
