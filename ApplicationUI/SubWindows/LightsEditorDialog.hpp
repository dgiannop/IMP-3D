#pragma once

#include <QColor>
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
    // Splitter UI
    void onToggleLeft();

    // List + CRUD
    void onLightSelectionChanged();
    void onAddLight();
    void onRemoveLight();

    // Right panel -> SceneLight
    void onNameEdited();
    void onEnabledToggled(bool checked);
    void onTypeChanged(int idx);
    void onPickColor();
    void onIntensityChanged(int v);
    void onRangeChanged(int v);
    void onSpotInnerChanged(int v);
    void onSpotOuterChanged(int v);

    // NEW: Flags -> SceneLight
    void onAffectRasterToggled(bool checked);
    void onAffectRtToggled(bool checked);
    void onCastShadowsToggled(bool checked);

private:
    void applyCollapsedState(bool collapsed, bool force = false);

    // ------------------------------------------------------------
    // Light list
    // ------------------------------------------------------------
    void    rebuildLightList(Core* core);
    void    restoreSelectionByName(const QString& name);
    QString currentSelectedName() const;

    SceneLight* selectedLight() const;

    // ------------------------------------------------------------
    // Right panel
    // ------------------------------------------------------------
    void setRightPanelEnabled(bool enabled);
    void loadLightToUi(const SceneLight* l);
    void updateColorSwatch(const QColor& c);

    // UI->value mapping helpers
    static int   intensityToUi(float v) noexcept; // Light.intensity -> slider [0..200]
    static float intensityFromUi(int v) noexcept; // slider -> Light.intensity
    static int   rangeToUi(float v) noexcept;     // Light.range -> slider [0..500]
    static float rangeFromUi(int v) noexcept;     // slider -> Light.range
    static int   coneToUi(float rad) noexcept;    // radians -> slider [0..100]
    static float coneFromUi(int v) noexcept;      // slider -> radians

    // Guard: avoid feedback loops when loading UI from model.
    struct UiGuard
    {
        explicit UiGuard(bool& flag) : m_flag(flag) { m_flag = true; }
        ~UiGuard() { m_flag = false; }
        bool& m_flag;
    };

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
    Core*    m_core           = nullptr;
    uint64_t m_lastSceneStamp = 0;

    // ------------------------------------------------------------
    // UI state
    // ------------------------------------------------------------
    bool   m_loadingUi   = false;
    QColor m_lastColorUi = QColor(255, 255, 255);
};
