#ifndef LIGHTINGSETTINGSDIALOG_HPP
#define LIGHTINGSETTINGSDIALOG_HPP

#include "SubWindowBase.hpp"

namespace Ui
{
    class LightingSettingsDialog;
} // namespace Ui

class LightingSettingsDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit LightingSettingsDialog(QWidget* parent = nullptr);
    ~LightingSettingsDialog() noexcept override;

    void idleEvent(class Core* core) override;

private:
    Ui::LightingSettingsDialog* ui = nullptr;
};

#endif // LIGHTINGSETTINGSDIALOG_HPP
