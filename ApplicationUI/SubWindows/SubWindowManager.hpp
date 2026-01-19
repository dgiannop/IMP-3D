#ifndef SUBWINDOWMANAGER_H
#define SUBWINDOWMANAGER_H

#include <Core.hpp>

#include "SubWindowBase.hpp"

class SubWindowManager : public QObject
{
    Q_OBJECT

public:
    SubWindowManager(QWidget* parent = nullptr) : QObject(parent)
    {
    }

    ~SubWindowManager() override
    {
        qDeleteAll(subWindows);
    }

    void addSubWindow(QString winName, SubWindowBase* subWindow)
    {
        subWindows[winName] = subWindow;
        subWindow->setProperty("name", winName);

        connect(subWindow, &SubWindowBase::onSubWindowFinished, this, [this, winName](int result) {
            emit onSubWindowClosed(winName, result);
        });
    }

    void showSubWindow(QString winName)
    {
        subWindows[winName]->show();
    }

    void hideSubWindow(QString winName)
    {
        subWindows[winName]->hide();
    }

    void idleEvent(Core* core)
    {
        for (auto& subWindow : subWindows)
        {
            subWindow->idleEvent(core);
        }
    }

signals:
    void onSubWindowClosed(QString winName, int result);

private:
    QHash<QString, SubWindowBase*> subWindows;
};

#endif // SUBWINDOWMANAGER_H
