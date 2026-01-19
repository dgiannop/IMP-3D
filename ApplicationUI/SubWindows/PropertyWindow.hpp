#ifndef PROPERTYWINDOW_HPP
#define PROPERTYWINDOW_HPP

#include <QDialog>

#include "SubWindowBase.hpp"

namespace Ui
{
class PropertyWindow;
}

class PropertyWindow : public SubWindowBase
{
    Q_OBJECT

public:
    explicit PropertyWindow(QWidget *parent = nullptr);
    ~PropertyWindow();

    void idleEvent(class Core* core) override;
private:
    Ui::PropertyWindow *ui;
    class Core* m_core;
};

#endif // PROPERTYWINDOW_HPP
