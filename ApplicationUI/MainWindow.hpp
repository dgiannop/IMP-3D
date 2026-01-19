#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>

#include "MenuStackedWidget.hpp"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class Core;
class QVulkanInstance;
class ViewportManager;
class SubWindowManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() noexcept override;

    /**
     * @brief Called when the main window is shown. Used for first-time setup.
     * @param event The show event.
     */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Initializes the side menu with buttons and sections.
     */
    void initSideMenu();

public slots:
    /**
     * @brief Called when a side menu button is clicked.
     * @param type The type of button clicked.
     * @param id The identifier associated with the button.
     * @param checked True if the button is checked.
     * @param delta Scroll or tab navigation direction.
     */
    void sideMenuButtonClicked(ButtonType type, const QString& id, bool checked, int delta = 0);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void handleAction();
    void onSubWindowClosed(QString name, int result);

private:
    Ui::MainWindow *ui;

    std::unique_ptr<Core>            m_core;
    std::unique_ptr<QVulkanInstance> m_vkInstance;

    QTimer* m_uiTimer = nullptr;
    void    onUiTick();

    std::unique_ptr<ViewportManager>  m_viewportManager;
    std::unique_ptr<SubWindowManager> m_subWindowManager;

    void enableVulkanValidationLayer();
};
#endif // MAINWINDOW_HPP
