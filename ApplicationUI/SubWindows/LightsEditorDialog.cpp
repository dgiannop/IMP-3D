#include "LightsEditorDialog.hpp"

#include <QAbstractButton>
#include <QColorDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QSplitter>
#include <algorithm>
#include <cmath>

#include "Core.hpp"
#include "SceneLight.hpp"
#include "SubWindows/ui_LightsEditorDialog.h"

namespace
{
    static QString to_qstring(std::string_view s)
    {
        return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
    }

    static std::string_view to_sv_temp(const QString& s, QByteArray& tmp)
    {
        tmp = s.toUtf8();
        return std::string_view(tmp.constData(), static_cast<size_t>(tmp.size()));
    }

    static int clamp_i(int v, int lo, int hi) noexcept
    {
        return std::max(lo, std::min(hi, v));
    }

    static float clamp_f(float v, float lo, float hi) noexcept
    {
        return std::max(lo, std::min(hi, v));
    }
} // namespace

LightsEditorDialog::LightsEditorDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::LightsEditorDialog)
{
    ui->setupUi(this);

    setWindowTitle("Lights Editor");

    setMinimumSize(520, 180);
    setMaximumSize(900, 800);

    m_expandedMinSize = minimumSize();
    m_expandedMaxSize = maximumSize();

    // Keep your fixed right panel width as designed in the .ui
    ui->rightPanel->setMinimumWidth(420);
    ui->rightPanel->setMaximumWidth(420);

    m_rightPanelMinW = ui->rightPanel->minimumWidth();
    m_rightPanelMaxW = ui->rightPanel->maximumWidth();

    m_leftIndex  = 0;
    m_rightIndex = 1;

    if (ui->splitterMain)
    {
        if (ui->splitterMain->count() >= 2)
        {
            for (int i = 0; i < ui->splitterMain->count(); ++i)
            {
                QWidget* w = ui->splitterMain->widget(i);
                if (!w)
                    continue;

                if (w->objectName() == "leftPanel")
                    m_leftIndex = i;
                else if (w->objectName() == "rightPanel")
                    m_rightIndex = i;
            }
        }

        ui->splitterMain->setStretchFactor(m_leftIndex, 1);
        ui->splitterMain->setStretchFactor(m_rightIndex, 0);
        ui->splitterMain->setCollapsible(m_leftIndex, true);
        ui->splitterMain->setCollapsible(m_rightIndex, false);
    }

    // Close button
    if (ui->closeButton)
        connect(ui->closeButton, &QAbstractButton::clicked, this, &QWidget::close);

    // Toggle left collapse
    if (ui->toggleLeftButton)
        connect(ui->toggleLeftButton, &QAbstractButton::clicked, this, &LightsEditorDialog::onToggleLeft);

    // Start expanded
    m_leftCollapsed    = false;
    m_lastExpandedSize = sizeHint();

    // Light list defaults + selection
    if (ui->lightList)
    {
        ui->lightList->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(ui->lightList, &QListWidget::currentRowChanged, this, &LightsEditorDialog::onLightSelectionChanged);
    }

    // CRUD buttons
    if (ui->bottomAddLightButton)
        connect(ui->bottomAddLightButton, &QAbstractButton::clicked, this, &LightsEditorDialog::onAddLight);
    if (ui->bottomRemoveLightButton)
        connect(ui->bottomRemoveLightButton, &QAbstractButton::clicked, this, &LightsEditorDialog::onRemoveLight);

    // Right panel signals
    if (ui->nameEdit)
        connect(ui->nameEdit, &QLineEdit::editingFinished, this, &LightsEditorDialog::onNameEdited);

    if (ui->enabledCheckBox)
        connect(ui->enabledCheckBox, &QCheckBox::toggled, this, &LightsEditorDialog::onEnabledToggled);

    if (ui->typeCombo)
        connect(ui->typeCombo, &QComboBox::currentIndexChanged, this, &LightsEditorDialog::onTypeChanged);

    if (ui->colorPickButton)
        connect(ui->colorPickButton, &QAbstractButton::clicked, this, &LightsEditorDialog::onPickColor);
    if (ui->colorSwatch)
        connect(ui->colorSwatch, &QAbstractButton::clicked, this, &LightsEditorDialog::onPickColor);

    if (ui->intensitySlider)
        connect(ui->intensitySlider, &QSlider::valueChanged, this, &LightsEditorDialog::onIntensityChanged);

    if (ui->rangeSlider)
        connect(ui->rangeSlider, &QSlider::valueChanged, this, &LightsEditorDialog::onRangeChanged);

    if (ui->spotInnerSlider)
        connect(ui->spotInnerSlider, &QSlider::valueChanged, this, &LightsEditorDialog::onSpotInnerChanged);

    if (ui->spotOuterSlider)
        connect(ui->spotOuterSlider, &QSlider::valueChanged, this, &LightsEditorDialog::onSpotOuterChanged);

    if (ui->affectRasterCheckBox)
        connect(ui->affectRasterCheckBox, &QCheckBox::toggled, this, &LightsEditorDialog::onAffectRasterToggled);
    if (ui->affectRtCheckBox)
        connect(ui->affectRtCheckBox, &QCheckBox::toggled, this, &LightsEditorDialog::onAffectRtToggled);
    if (ui->castShadowsCheckBox)
        connect(ui->castShadowsCheckBox, &QCheckBox::toggled, this, &LightsEditorDialog::onCastShadowsToggled);

    // For now disable them untill I pass them to the Renderer/Shaders
    if (ui->affectRasterCheckBox)
        ui->affectRasterCheckBox->setEnabled(false);
    if (ui->affectRtCheckBox)
        ui->affectRtCheckBox->setEnabled(false);
    if (ui->castShadowsCheckBox)
        ui->castShadowsCheckBox->setEnabled(false);

    setRightPanelEnabled(false);
}

LightsEditorDialog::~LightsEditorDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void LightsEditorDialog::idleEvent(Core* core)
{
    if (!core || !ui)
        return;

    m_core = core;

    const uint64_t stamp = core->sceneChangeStamp();
    if (stamp == m_lastSceneStamp)
        return;

    m_lastSceneStamp = stamp;

    // Keep selection stable by name.
    const QString prevSel = currentSelectedName();
    rebuildLightList(core);
    restoreSelectionByName(prevSel);

    // Update right panel from selection.
    onLightSelectionChanged();
}

// ------------------------------------------------------------
// Splitter UI
// ------------------------------------------------------------

void LightsEditorDialog::onToggleLeft()
{
    applyCollapsedState(!m_leftCollapsed);
}

void LightsEditorDialog::applyCollapsedState(bool collapsed, bool force)
{
    if (!ui || !ui->splitterMain || !ui->rightPanel || !ui->leftPanel)
        return;

    if (!force && collapsed == m_leftCollapsed)
        return;

    if (!m_leftCollapsed && collapsed)
        m_lastExpandedSize = size();

    m_leftCollapsed = collapsed;

    if (collapsed)
    {
        ui->leftPanel->setVisible(false);

        // Let right panel fill the dialog in collapsed mode
        ui->rightPanel->setMinimumWidth(0);
        ui->rightPanel->setMaximumWidth(QWIDGETSIZE_MAX);
        ui->rightPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QList<int> sizes;
        sizes.reserve(ui->splitterMain->count());
        for (int i = 0; i < ui->splitterMain->count(); ++i)
            sizes.push_back((i == m_rightIndex) ? 1 : 0);
        ui->splitterMain->setSizes(sizes);

        const int marginW =
            (layout() ? (layout()->contentsMargins().left() + layout()->contentsMargins().right()) : 0) +
            (ui->splitterMain ? (ui->splitterMain->contentsMargins().left() + ui->splitterMain->contentsMargins().right()) : 0);

        const int newW = ui->rightPanel->sizeHint().width() + marginW + 2;
        setFixedWidth(std::max(newW, minimumSizeHint().width()));
        resize(width(), height());
    }
    else
    {
        ui->leftPanel->setVisible(true);

        ui->rightPanel->setMinimumWidth(m_rightPanelMinW);
        ui->rightPanel->setMaximumWidth(m_rightPanelMaxW);
        ui->rightPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        setMinimumSize(m_expandedMinSize);
        setMaximumSize(m_expandedMaxSize);

        const int rightW = ui->rightPanel->maximumWidth();

        if (m_lastExpandedSize.width() < rightW + 100)
            m_lastExpandedSize.setWidth(rightW + 180);
        if (m_lastExpandedSize.height() < minimumHeight())
            m_lastExpandedSize.setHeight(minimumHeight());

        resize(m_lastExpandedSize);

        const int totalW = width();
        const int leftW  = std::max(0, totalW - rightW);

        QList<int> sizes;
        sizes.reserve(ui->splitterMain->count());
        for (int i = 0; i < ui->splitterMain->count(); ++i)
        {
            if (i == m_leftIndex)
                sizes.push_back(leftW);
            else if (i == m_rightIndex)
                sizes.push_back(rightW);
            else
                sizes.push_back(0);
        }
        ui->splitterMain->setSizes(sizes);
    }
}

// ------------------------------------------------------------
// Light list
// ------------------------------------------------------------

void LightsEditorDialog::rebuildLightList(Core* core)
{
    if (!ui || !ui->lightList || !core)
        return;

    const QString prevSel = currentSelectedName();

    ui->lightList->blockSignals(true);
    ui->lightList->clear();

    const std::vector<SceneLight*> lights = core->sceneLights();
    for (SceneLight* l : lights)
    {
        if (!l)
            continue;

        ui->lightList->addItem(new QListWidgetItem(to_qstring(l->name())));
    }

    ui->lightList->blockSignals(false);

    restoreSelectionByName(prevSel);

    if (ui->lightList->currentRow() < 0 && ui->lightList->count() > 0)
        ui->lightList->setCurrentRow(0);
}

QString LightsEditorDialog::currentSelectedName() const
{
    if (!ui || !ui->lightList)
        return {};

    if (QListWidgetItem* it = ui->lightList->currentItem())
        return it->text();

    return {};
}

void LightsEditorDialog::restoreSelectionByName(const QString& name)
{
    if (!ui || !ui->lightList)
        return;

    const QString key = name.trimmed();
    if (key.isEmpty())
        return;

    for (int i = 0; i < ui->lightList->count(); ++i)
    {
        QListWidgetItem* it = ui->lightList->item(i);
        if (!it)
            continue;

        if (QString::compare(it->text(), key, Qt::CaseInsensitive) == 0)
        {
            ui->lightList->setCurrentRow(i);
            return;
        }
    }
}

SceneLight* LightsEditorDialog::selectedLight() const
{
    if (!m_core || !ui || !ui->lightList)
        return nullptr;

    const int row = ui->lightList->currentRow();
    if (row < 0)
        return nullptr;

    const std::vector<SceneLight*> lights = m_core->sceneLights();
    if (row >= static_cast<int>(lights.size()))
        return nullptr;

    return lights[static_cast<size_t>(row)];
}

void LightsEditorDialog::onLightSelectionChanged()
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
    {
        setRightPanelEnabled(false);
        return;
    }

    loadLightToUi(l);
    setRightPanelEnabled(true);
}

// ------------------------------------------------------------
// CRUD
// ------------------------------------------------------------

void LightsEditorDialog::onAddLight()
{
    if (!m_core)
        return;

    // Simple unique-ish name
    int     i = 1;
    QString name;
    for (;;)
    {
        name        = QString("Light %1").arg(i++);
        bool exists = false;

        const auto lights = m_core->sceneLights();
        for (const SceneLight* l : lights)
        {
            if (l && QString::compare(to_qstring(l->name()), name, Qt::CaseInsensitive) == 0)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
            break;
    }

    QByteArray tmp;
    (void)m_core->createLight(to_sv_temp(name, tmp), LightType::Point);

    // Force refresh immediately.
    m_lastSceneStamp = 0;
}

void LightsEditorDialog::onRemoveLight()
{
    // Core currently doesn't expose removeLight(). So disable remove for now.
    // If/when you add Core::destroyLight(LightId), wire it here.
}

// ------------------------------------------------------------
// Right panel helpers
// ------------------------------------------------------------

void LightsEditorDialog::setRightPanelEnabled(bool enabled)
{
    if (!ui)
        return;

    if (ui->propsFrame)
        ui->propsFrame->setEnabled(enabled);
}

void LightsEditorDialog::updateColorSwatch(const QColor& c)
{
    if (!ui || !ui->colorSwatch)
        return;

    m_lastColorUi = c;

    const QString css = QString("background-color: %1;").arg(c.name(QColor::HexRgb));
    ui->colorSwatch->setStyleSheet(css);
}

int LightsEditorDialog::intensityToUi(float v) noexcept
{
    // Map [0..10] -> [0..200] by default (0.05 per tick)
    const float clamped = clamp_f(v, 0.0f, 10.0f);
    return clamp_i(static_cast<int>(std::lround(clamped * 20.0f)), 0, 200);
}

float LightsEditorDialog::intensityFromUi(int v) noexcept
{
    const int iv = clamp_i(v, 0, 200);
    return static_cast<float>(iv) / 20.0f;
}

int LightsEditorDialog::rangeToUi(float v) noexcept
{
    const float clamped = clamp_f(v, 0.0f, 500.0f);
    return clamp_i(static_cast<int>(std::lround(clamped)), 0, 500);
}

float LightsEditorDialog::rangeFromUi(int v) noexcept
{
    return static_cast<float>(clamp_i(v, 0, 500));
}

int LightsEditorDialog::coneToUi(float rad) noexcept
{
    // Slider [0..100] maps to [0..pi/2]
    const float maxRad  = 1.57079632679f;
    const float clamped = clamp_f(rad, 0.0f, maxRad);
    return clamp_i(static_cast<int>(std::lround((clamped / maxRad) * 100.0f)), 0, 100);
}

float LightsEditorDialog::coneFromUi(int v) noexcept
{
    const float maxRad = 1.57079632679f;
    const int   iv     = clamp_i(v, 0, 100);
    return (static_cast<float>(iv) / 100.0f) * maxRad;
}

void LightsEditorDialog::loadLightToUi(const SceneLight* l)
{
    if (!ui || !l)
        return;

    UiGuard guard(m_loadingUi);

    // Block individual widgets to avoid unwanted signal cascades.
    const QSignalBlocker b0(ui->nameEdit);
    const QSignalBlocker b1(ui->enabledCheckBox);
    const QSignalBlocker b2(ui->typeCombo);
    const QSignalBlocker b3(ui->intensitySlider);
    const QSignalBlocker b4(ui->rangeSlider);
    const QSignalBlocker b5(ui->spotInnerSlider);
    const QSignalBlocker b6(ui->spotOuterSlider);
    const QSignalBlocker b7(ui->affectRasterCheckBox);
    const QSignalBlocker b8(ui->affectRtCheckBox);
    const QSignalBlocker b9(ui->castShadowsCheckBox);

    ui->nameEdit->setText(to_qstring(l->name()));
    ui->enabledCheckBox->setChecked(l->enabled());

    // Type combo is [Directional, Point, Spot]
    int typeIdx = 0;
    switch (l->lightType())
    {
        case LightType::Directional:
            typeIdx = 0;
            break;
        case LightType::Point:
            typeIdx = 1;
            break;
        case LightType::Spot:
            typeIdx = 2;
            break;
        default:
            typeIdx = 1;
            break;
    }
    ui->typeCombo->setCurrentIndex(typeIdx);

    ui->intensitySlider->setValue(intensityToUi(l->intensity()));
    ui->rangeSlider->setValue(rangeToUi(l->range()));
    ui->spotInnerSlider->setValue(coneToUi(l->spotInnerConeRad()));
    ui->spotOuterSlider->setValue(coneToUi(l->spotOuterConeRad()));

    // NEW: Flags (assumes SceneLight exposes these accessors)
    if (ui->affectRasterCheckBox)
        ui->affectRasterCheckBox->setChecked(l->affectRaster());
    if (ui->affectRtCheckBox)
        ui->affectRtCheckBox->setChecked(l->affectRt());
    if (ui->castShadowsCheckBox)
        ui->castShadowsCheckBox->setChecked(l->castShadows());

    // Disable irrelevant controls by type (still show values).
    const bool isDir  = (l->lightType() == LightType::Directional);
    const bool isSpot = (l->lightType() == LightType::Spot);

    ui->rangeSlider->setEnabled(!isDir);
    ui->spotInnerSlider->setEnabled(isSpot);
    ui->spotOuterSlider->setEnabled(isSpot);

    const glm::vec3 c = l->color();
    updateColorSwatch(QColor::fromRgbF(
        clamp_f(c.x, 0.0f, 1.0f),
        clamp_f(c.y, 0.0f, 1.0f),
        clamp_f(c.z, 0.0f, 1.0f)));
}

// ------------------------------------------------------------
// Right panel slots -> model
// ------------------------------------------------------------

void LightsEditorDialog::onNameEdited()
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    const QString q = ui->nameEdit ? ui->nameEdit->text().trimmed() : QString{};
    if (q.isEmpty())
    {
        loadLightToUi(l);
        return;
    }

    QByteArray tmp;
    l->name(to_sv_temp(q, tmp));

    // Refresh list immediately so rename shows on the left.
    rebuildLightList(m_core);
    restoreSelectionByName(q);
}

void LightsEditorDialog::onEnabledToggled(bool checked)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->enabled(checked);
}

void LightsEditorDialog::onTypeChanged(int idx)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    LightType t = LightType::Point;
    if (idx == 0)
        t = LightType::Directional;
    if (idx == 1)
        t = LightType::Point;
    if (idx == 2)
        t = LightType::Spot;

    l->lightType(t);

    // Update enable/disable of related controls.
    loadLightToUi(l);
}

void LightsEditorDialog::onPickColor()
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    QColor          initial = m_lastColorUi;
    const glm::vec3 cc      = l->color();
    initial                 = QColor::fromRgbF(
        clamp_f(cc.x, 0.0f, 1.0f),
        clamp_f(cc.y, 0.0f, 1.0f),
        clamp_f(cc.z, 0.0f, 1.0f));

    QColor chosen = QColorDialog::getColor(initial, this, "Light Color", QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;

    updateColorSwatch(chosen);

    l->color(glm::vec3(
        static_cast<float>(chosen.redF()),
        static_cast<float>(chosen.greenF()),
        static_cast<float>(chosen.blueF())));
}

void LightsEditorDialog::onIntensityChanged(int v)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->intensity(intensityFromUi(v));
}

void LightsEditorDialog::onRangeChanged(int v)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->range(rangeFromUi(v));
}

void LightsEditorDialog::onSpotInnerChanged(int v)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->spotInnerConeRad(coneFromUi(v));
}

void LightsEditorDialog::onSpotOuterChanged(int v)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->spotOuterConeRad(coneFromUi(v));
}

// ------------------------------------------------------------
// NEW: Flags slots -> model
// ------------------------------------------------------------

void LightsEditorDialog::onAffectRasterToggled(bool checked)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->affectRaster(checked);
}

void LightsEditorDialog::onAffectRtToggled(bool checked)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->affectRt(checked);
}

void LightsEditorDialog::onCastShadowsToggled(bool checked)
{
    if (m_loadingUi)
        return;

    SceneLight* l = selectedLight();
    if (!l)
        return;

    l->castShadows(checked);
}
