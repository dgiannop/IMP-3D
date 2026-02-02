#ifndef SCENESTATSDIALOG_HPP
#define SCENESTATSDIALOG_HPP

#include <QWidget>

#include "SubWindows/SubWindowBase.hpp"

namespace Ui
{
    class SceneStatsDialog;
} // namespace Ui

class SceneStatsDialog : public SubWindowBase
{
    Q_OBJECT

public:
    explicit SceneStatsDialog(QWidget* parent = nullptr);
    ~SceneStatsDialog();

    void idleEvent(class Core* core) override;

private:
    Ui::SceneStatsDialog* ui;
    uint64_t              m_lastStamp = 0ull;
};

#endif // SCENESTATSDIALOG_HPP
