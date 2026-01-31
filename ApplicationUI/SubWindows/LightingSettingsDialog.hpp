#ifndef LIGHTINGSETTINGSDIALOG_HPP
#define LIGHTINGSETTINGSDIALOG_HPP

#include "SubWindowBase.hpp"

namespace Ui
{
    class LightingSettingsDialog;
} // namespace Ui

class Core;

/**
 * @class LightingSettingsDialog
 * @brief Dockable sub-window for editing scene lighting settings.
 *
 * This dialog reads and writes LightingSettings via Core. It does not
 * access Scene or Renderer directly.
 */
class LightingSettingsDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit LightingSettingsDialog(QWidget* parent = nullptr);
    ~LightingSettingsDialog() noexcept override;

    /**
     * @brief Periodic UI sync entry point.
     *
     * Called by the host application during idle. The dialog uses this
     * to pull the latest LightingSettings from Core (e.g. after file
     * load, reset, undo) and update its widgets.
     */
    void idleEvent(Core* core) override;

private:
    /**
     * @brief Push current widget values into Core's LightingSettings.
     *
     * This is called in response to user interaction (sliders, checkboxes,
     * combos). It reads the current settings from Core, modifies them,
     * and writes them back via Core::setLightingSettings().
     */
    void pushToCore();

    /**
     * @brief Pull LightingSettings from Core and update widgets.
     *
     * This is called from idleEvent() to keep the UI in sync with the
     * scene. Updates are guarded by m_blockUi to avoid feedback loops.
     */
    void pullFromCore();

private:
    Ui::LightingSettingsDialog* ui = nullptr;

    /** @brief Last Core pointer provided by idleEvent(). */
    Core* m_core = nullptr;

    /** @brief Guard flag to avoid recursive push/pull during UI updates. */
    bool m_blockUi = false;

    /** @brief Last observed scene stamp for UI refresh gating. */
    uint64_t m_lastSceneStamp = 0;
};

#endif // LIGHTINGSETTINGSDIALOG_HPP
