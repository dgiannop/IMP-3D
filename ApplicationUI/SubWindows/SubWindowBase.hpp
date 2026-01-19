#ifndef SUBWINDOWBASE_HPP
#define SUBWINDOWBASE_HPP

#include <qmainwindow.h>

#include <QDialog>
#include <QIcon>

#include "Core.hpp"

class SubWindowBase : public QDialog
{
    Q_OBJECT

public:
    explicit SubWindowBase(QWidget* parent = nullptr) : QDialog(parent)
    {
        // Set window flags to ensure it has an icon, close button, and is resizable
        setWindowFlags(Qt::Dialog | Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint);

        // Make the window resizable
        setSizeGripEnabled(true);

        // Set the main window icon
        QIcon mainWindowIcon = qobject_cast<QMainWindow*>(parent)->windowIcon();
        setWindowIcon(mainWindowIcon);
        setSizeGripEnabled(false);
        connect(this, &QDialog::finished, this, &SubWindowBase::onSubWindowFinished);
    }

    virtual ~SubWindowBase() noexcept = default;

    virtual void idleEvent(Core* core) = 0;

signals:
    void onSubWindowFinished(int result);
};

#endif // SUBWINDOWBASE_HPP
