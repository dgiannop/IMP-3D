//==============================================================
// LightsEditorDialog.hpp
//==============================================================
#pragma once

#include <QSize>
#include <QString>
#include <SysCounter.hpp>

#include "SubWindowBase.hpp"

class Core;
class SceneLight;

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

    void idleEvent(Core* core) override;

private slots:
    void onToggleLeft();

private:
    void applyCollapsedState(bool collapsed, bool force = false);

    // ------------------------------------------------------------
    // Light list
    // ------------------------------------------------------------
    void    rebuildLightList(Core* core);
    void    restoreSelectionByName(const QString& name);
    QString currentSelectedName() const;

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

    // ------------------------------------------------------------
    // Cached core + change tracking
    // ------------------------------------------------------------
    Core*         m_core             = nullptr;
    SysCounterPtr m_lastSceneCounter = {};
    uint64_t      m_lastSceneStamp   = 0;
    bool          m_hasInitialList   = false;
};
