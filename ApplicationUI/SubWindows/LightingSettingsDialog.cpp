#include "LightingSettingsDialog.hpp"

#include <QAbstractButton>

#include "SubWindows/ui_LightingSettingsDialog.h"

LightingSettingsDialog::LightingSettingsDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::LightingSettingsDialog)
{
    ui->setupUi(this);

    setWindowTitle("Lighting");

    // Optional: wire Close if present in the .ui
    if (ui->closeButton)
        connect(ui->closeButton, &QAbstractButton::clicked, this, &QWidget::close);

    // Optional: wire Reset if present (no behavior yet)
    // if (ui->resetButton)
    //     connect(ui->resetButton, &QAbstractButton::clicked, this, []() {});
}

LightingSettingsDialog::~LightingSettingsDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void LightingSettingsDialog::idleEvent(Core* /*core*/)
{
    // UI-only for now.
}
