//==============================================================
// LightsEditorDialog.hpp
//==============================================================
#pragma once

#include <QSize>

#include "SubWindowBase.hpp"

namespace Ui
{
    class LightsEditorDialog;
} // namespace Ui

class LightsEditorDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit LightsEditorDialog(QWidget* parent = nullptr);
    ~LightsEditorDialog() noexcept override;

    void idleEvent(class Core* core) override;

private slots:
    void onToggleLeft();

private:
    void applyCollapsedState(bool collapsed, bool force = false);

private:
    Ui::LightsEditorDialog* ui = nullptr;

    bool  m_leftCollapsed    = false;
    QSize m_lastExpandedSize = {};

    int m_leftIndex  = 0;
    int m_rightIndex = 1;

    int m_rightPanelMinW = 0;
    int m_rightPanelMaxW = 0;

    QSize m_expandedMinSize;
    QSize m_expandedMaxSize;
};
