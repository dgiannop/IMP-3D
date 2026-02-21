//============================================================
// LightingSettingsDialog.cpp  (FULL REPLACEMENT)
//============================================================
#include "LightingSettingsDialog.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>

#include "Core.hpp"
#include "LightingSettings.hpp"
#include "SubWindows/ui_LightingSettingsDialog.h"

// ------------------------------------------------------------
// Construction / destruction
// ------------------------------------------------------------

LightingSettingsDialog::LightingSettingsDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::LightingSettingsDialog)
{
    ui->setupUi(this);
    setWindowTitle("Lighting");

    // Close button simply hides the window.
    if (ui->closeButton)
    {
        connect(ui->closeButton, &QAbstractButton::clicked, this, &QWidget::close);
    }

    // Reset button restores default LightingSettings (value-initialized).
    if (ui->resetButton)
    {
        connect(ui->resetButton, &QAbstractButton::clicked, this, [this]() {
            if (!m_core)
                return;

            LightingSettings defaults = {};
            m_core->setLightingSettings(defaults);
        });
    }

    // Helper lambdas to wire "any change → pushToCore()".
    auto connectCheck = [this](QCheckBox* cb) {
        if (!cb)
            return;

        connect(cb, &QCheckBox::toggled, this, [this](bool) {
            if (!m_blockUi)
                pushToCore();
        });
    };

    auto connectSlider = [this](QSlider* s) {
        if (!s)
            return;

        connect(s, &QSlider::valueChanged, this, [this](int) {
            if (!m_blockUi)
                pushToCore();
        });
    };

    auto connectCombo = [this](QComboBox* c) {
        if (!c)
            return;

        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (!m_blockUi)
                pushToCore();
        });
    };

    // Sources
    connectCheck(ui->useHeadlightCheck);
    connectCheck(ui->useSceneLightsCheck);
    connectSlider(ui->headlightIntensitySlider);
    connectSlider(ui->ambientFillSlider);

    // Scene light tuning
    connectSlider(ui->scenePointIntensityMulSlider);
    connectSlider(ui->scenePointRangeMulSlider);
    connectSlider(ui->sceneSpotIntensityMulSlider);
    connectSlider(ui->sceneSpotRangeMulSlider);
    connectSlider(ui->sceneSpotConeMulSlider);

    // Exposure & tonemap
    connectSlider(ui->exposureSlider);
    connectCheck(ui->tonemapCheck);

    // Mode policy
    connectCombo(ui->solidModeCombo);
    connectCombo(ui->shadedModeCombo);
    connectCombo(ui->rtModeCombo);

    // Debug
    connectCheck(ui->clampCheck);
    connectSlider(ui->clampMaxSlider);
}

LightingSettingsDialog::~LightingSettingsDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

// ------------------------------------------------------------
// Idle sync
// ------------------------------------------------------------

void LightingSettingsDialog::idleEvent(Core* core)
{
    m_core = core;
    if (!m_core)
        return;

    const uint64_t stamp = m_core->sceneChangeStamp();

    // First run (or after a reset) forces a pull.
    if (m_lastSceneStamp == 0 || stamp != m_lastSceneStamp)
    {
        pullFromCore();
        m_lastSceneStamp = stamp;
    }
}

// ------------------------------------------------------------
// UI -> Core
// ------------------------------------------------------------

void LightingSettingsDialog::pushToCore()
{
    if (!m_core)
        return;

    // Start from current core settings to preserve any fields not
    // represented in this dialog (future-proof).
    LightingSettings s = m_core->lightingSettings();

    // Sources
    s.useHeadlight       = ui->useHeadlightCheck->isChecked();
    s.useSceneLights     = ui->useSceneLightsCheck->isChecked();
    s.headlightIntensity = ui->headlightIntensitySlider->value() / 100.0f;
    s.ambientFill        = ui->ambientFillSlider->value() / 100.0f;

    // Scene light tuning
    s.scenePointIntensityMul = ui->scenePointIntensityMulSlider->value() / 100.0f;
    s.scenePointRangeMul     = ui->scenePointRangeMulSlider->value() / 100.0f;

    s.sceneSpotIntensityMul = ui->sceneSpotIntensityMulSlider->value() / 100.0f;
    s.sceneSpotRangeMul     = ui->sceneSpotRangeMulSlider->value() / 100.0f;
    s.sceneSpotConeMul      = ui->sceneSpotConeMulSlider->value() / 100.0f;

    // Exposure & tonemap
    s.exposure = ui->exposureSlider->value() / 100.0f;
    s.tonemap  = ui->tonemapCheck->isChecked();

    // Mode policy (combo indices map 1:1 to enum values)
    s.solidPolicy  = LightingSettings::ModePolicy(ui->solidModeCombo->currentIndex());
    s.shadedPolicy = LightingSettings::ModePolicy(ui->shadedModeCombo->currentIndex());
    s.rtPolicy     = LightingSettings::ModePolicy(ui->rtModeCombo->currentIndex());

    // Debug
    s.clampRadiance = ui->clampCheck->isChecked();
    s.clampMax      = ui->clampMaxSlider->value() / 10.0f;

    m_core->setLightingSettings(s);
}

// ------------------------------------------------------------
// Core -> UI
// ------------------------------------------------------------

void LightingSettingsDialog::pullFromCore()
{
    if (!m_core)
        return;

    const LightingSettings s = m_core->lightingSettings();

    // Prevent signal handlers from pushing back into Core while
    // we are programmatically updating controls.
    m_blockUi = true;

    // Sources
    ui->useHeadlightCheck->setChecked(s.useHeadlight);
    ui->useSceneLightsCheck->setChecked(s.useSceneLights);
    ui->headlightIntensitySlider->setValue(int(s.headlightIntensity * 100.0f));
    ui->ambientFillSlider->setValue(int(s.ambientFill * 100.0f));

    // Scene light tuning
    ui->scenePointIntensityMulSlider->setValue(int(s.scenePointIntensityMul * 100.0f));
    ui->scenePointRangeMulSlider->setValue(int(s.scenePointRangeMul * 100.0f));

    ui->sceneSpotIntensityMulSlider->setValue(int(s.sceneSpotIntensityMul * 100.0f));
    ui->sceneSpotRangeMulSlider->setValue(int(s.sceneSpotRangeMul * 100.0f));
    ui->sceneSpotConeMulSlider->setValue(int(s.sceneSpotConeMul * 100.0f));

    // Exposure & tonemap
    ui->exposureSlider->setValue(int(s.exposure * 100.0f));
    ui->tonemapCheck->setChecked(s.tonemap);

    // Mode policy
    ui->solidModeCombo->setCurrentIndex(int(s.solidPolicy));
    ui->shadedModeCombo->setCurrentIndex(int(s.shadedPolicy));
    ui->rtModeCombo->setCurrentIndex(int(s.rtPolicy));

    // Debug
    ui->clampCheck->setChecked(s.clampRadiance);
    ui->clampMaxSlider->setValue(int(s.clampMax * 10.0f));

    m_blockUi = false;
}
