#ifndef VIEWPORTWIDGET_HPP
#define VIEWPORTWIDGET_HPP

#include <QWidget>

#include "CoreTypes.hpp"

namespace Ui
{
    class ViewportWidget;
} // namespace Ui

class Core;
class Viewport;

class VulkanBackend;        // UI layer type (Qt/Vulkan)
class ViewportRenderWindow; // UI layer type (QWindow)

class ViewportWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr, Core* core = nullptr, VulkanBackend* backend = nullptr);
    ~ViewportWidget() override;

    QWidget* renderHost() const;

    void requestRender();

    Viewport* coreViewport() const noexcept
    {
        return m_viewport;
    }

    void setInitialViewMode(ViewMode mode) noexcept;
    void setInitialDrawMode(DrawMode mode) noexcept;

    void shutdownVulkan() noexcept;

protected:
    void scrollButtonAction(QWidget* sender, QPoint delta);

signals:
    void expandViewportAction(QWidget* sender);

private slots:
    void cmbViewTypeChanged(int index);
    void cmbDrawTypeChanged(int index);

private:
    Ui::ViewportWidget* ui         = nullptr;
    Core*               m_core     = nullptr;
    Viewport*           m_viewport = nullptr;

    VulkanBackend*        m_backend   = nullptr;
    ViewportRenderWindow* m_window    = nullptr;
    QWidget*              m_container = nullptr;
};

#endif // VIEWPORTWIDGET_HPP
