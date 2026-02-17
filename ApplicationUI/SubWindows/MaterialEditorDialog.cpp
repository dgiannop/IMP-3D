#include "MaterialEditorDialog.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QSplitter>
#include <algorithm>
#include <cmath>

#include "Core.hpp"
#include "ImageHandler.hpp"
#include "Material.hpp"
#include "MaterialEditor.hpp"
#include "SubWindows/ui_MaterialEditorDialog.h"

namespace
{
    static constexpr int kRoleMaterialId = Qt::UserRole + 1;

    static int clamp_int(int v, int lo, int hi) noexcept
    {
        return std::clamp(v, lo, hi);
    }

    static float clamp_float(float v, float lo, float hi) noexcept
    {
        return std::clamp(v, lo, hi);
    }

    static float slider_to_float(int v, int smin, int smax, float fmin, float fmax) noexcept
    {
        if (smax <= smin)
            return fmin;

        v             = clamp_int(v, smin, smax);
        const float t = float(v - smin) / float(smax - smin);
        return fmin + (fmax - fmin) * t;
    }

    static int slider_from_float(float x, float fmin, float fmax, int smin, int smax) noexcept
    {
        if (smax <= smin)
            return smin;

        x              = clamp_float(x, fmin, fmax);
        const float t  = (x - fmin) / (fmax - fmin);
        const float sv = float(smin) + t * float(smax - smin);
        return clamp_int(int(sv + 0.5f), smin, smax);
    }

    static float slider_to_01(int v, int smin, int smax) noexcept
    {
        return slider_to_float(v, smin, smax, 0.0f, 1.0f);
    }

    static int slider_from_01(float t, int smin, int smax) noexcept
    {
        return slider_from_float(t, 0.0f, 1.0f, smin, smax);
    }

    static float perceptual_to_roughness(float t) noexcept
    {
        t = clamp_float(t, 0.0f, 1.0f);
        return t * t;
    }

    static float roughness_to_perceptual(float r) noexcept
    {
        r = clamp_float(r, 0.0f, 1.0f);
        return std::sqrt(r);
    }

    static QColor to_qcolor(const glm::vec3& c) noexcept
    {
        const int r = int(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        const int g = int(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        const int b = int(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        return QColor(r, g, b);
    }

    static glm::vec3 from_qcolor(const QColor& c) noexcept
    {
        return glm::vec3(float(c.red()) / 255.0f, float(c.green()) / 255.0f, float(c.blue()) / 255.0f);
    }

    static void set_swatch(QWidget* w, const QColor& c)
    {
        // if (!w)
        //     return;

        // // For QFrame/QWidget swatches: stylesheet is deterministic.
        // // Avoid setAutoFillBackground() / palettes; they interact poorly with QSS.
        // const QString css =
        //     QString("background-color: %1; border: 1px solid rgba(255,255,255,40);").arg(c.name(QColor::HexRgb));
        // w->setStyleSheet(css);
        if (!w)
            return;

        w->setAutoFillBackground(true);

        QPalette pal = w->palette();
        pal.setColor(QPalette::Button, c);
        pal.setColor(QPalette::Window, c);
        w->setPalette(pal);

        // Important: stop injecting per-widget border lines.
        w->setStyleSheet(QString("background-color: %1; border: none;").arg(c.name(QColor::HexRgb)));
    }

    static void set_fixed_row_height(QWidget* w, int h)
    {
        if (!w)
            return;
        w->setMinimumHeight(h);
        w->setMaximumHeight(h);
    }

    static void tune_combo_popup(QComboBox* cb, int maxVisibleItems)
    {
        if (!cb)
            return;

        cb->setMaxVisibleItems(maxVisibleItems);
        if (cb->view())
        {
            cb->view()->setMinimumHeight(0);
            cb->view()->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }

    struct SliderRange
    {
        int   smin = 0;
        int   smax = 100;
        float fmin = 0.0f;
        float fmax = 1.0f;
    };

    static constexpr SliderRange kMetallicRange    = {0, 100, 0.0f, 1.0f};
    static constexpr SliderRange kOpacityRange     = {0, 100, 0.0f, 1.0f};
    static constexpr SliderRange kRoughnessUiRange = {0, 100, 0.0f, 1.0f};
    static constexpr SliderRange kIorRange         = {100, 300, 1.0f, 3.0f};

    // Emissive intensity: keep it simple for now: 0..2 mapped to 0..200 slider.
    static constexpr SliderRange kEmissiveIntRange = {0, 200, 0.0f, 2.0f};

} // namespace

MaterialEditorDialog::MaterialEditorDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::MaterialEditorDialog),
    m_lastImagesCounter{~0ull}
{
    ui->setupUi(this);

    setWindowTitle("Material Editor");

    // Prevent "pic1" shrink: keep a sane minimum when expanded.
    // Collapsed mode is handled in applyCollapsedState().
    setMinimumSize(760, 480);
    setMaximumSize(900, 900);

    m_expandedMinSize = minimumSize();
    m_expandedMaxSize = maximumSize();

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

    if (ui->metallicSlider)
        ui->metallicSlider->setRange(kMetallicRange.smin, kMetallicRange.smax);
    if (ui->roughnessSlider)
        ui->roughnessSlider->setRange(kRoughnessUiRange.smin, kRoughnessUiRange.smax);
    if (ui->opacitySlider)
        ui->opacitySlider->setRange(kOpacityRange.smin, kOpacityRange.smax);
    if (ui->iorSlider)
        ui->iorSlider->setRange(kIorRange.smin, kIorRange.smax);
    if (ui->emissiveIntensitySlider)
        ui->emissiveIntensitySlider->setRange(kEmissiveIntRange.smin, kEmissiveIntRange.smax);

    if (ui->toggleLeftButton)
        connect(ui->toggleLeftButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onToggleLeft);

    if (ui->materialList)
        connect(ui->materialList,
                &QListWidget::currentItemChanged,
                this,
                &MaterialEditorDialog::onMaterialSelectionChanged);

    if (ui->assignButton)
        connect(ui->assignButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onAssignClicked);

    if (ui->nameEdit)
        connect(ui->nameEdit, &QLineEdit::editingFinished, this, &MaterialEditorDialog::onNameEdited);

    // Slider -> material
    if (ui->metallicSlider)
        connect(ui->metallicSlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onMetallicChanged);
    if (ui->roughnessSlider)
        connect(ui->roughnessSlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onRoughnessChanged);
    if (ui->iorSlider)
        connect(ui->iorSlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onIorChanged);
    if (ui->opacitySlider)
        connect(ui->opacitySlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onOpacityChanged);
    if (ui->emissiveIntensitySlider)
        connect(ui->emissiveIntensitySlider,
                &QSlider::valueChanged,
                this,
                &MaterialEditorDialog::onEmissiveIntensityChanged);

    // Spin -> material
    if (ui->metallicSpin)
        connect(ui->metallicSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialEditorDialog::onMetallicSpinChanged);
    if (ui->roughnessSpin)
        connect(ui->roughnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialEditorDialog::onRoughnessSpinChanged);
    if (ui->iorSpin)
        connect(ui->iorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialEditorDialog::onIorSpinChanged);
    if (ui->opacitySpin)
        connect(ui->opacitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialEditorDialog::onOpacitySpinChanged);
    if (ui->emissiveIntensitySpin)
        connect(ui->emissiveIntensitySpin,
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,
                &MaterialEditorDialog::onEmissiveIntensitySpinChanged);

    if (ui->baseColorPickButton)
        connect(ui->baseColorPickButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onPickBaseColor);
    if (ui->emissivePickButton)
        connect(ui->emissivePickButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onPickEmissive);

    // Populate combos with "None" now; rebuilt when Core is available.
    initMapCombos();

    if (ui->baseMapCombo)
        connect(ui->baseMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onBaseMapChanged);
    if (ui->normalMapCombo)
        connect(ui->normalMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onNormalMapChanged);
    if (ui->metallicMapCombo)
        connect(ui->metallicMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onMetallicMapChanged);
    if (ui->roughnessMapCombo)
        connect(ui->roughnessMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onRoughnessMapChanged);
    if (ui->aoMapCombo)
        connect(ui->aoMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onAoMapChanged);
    if (ui->emissiveMapCombo)
        connect(ui->emissiveMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onEmissiveMapChanged);

    // MRAO display combo is disabled; no signal.

    m_leftCollapsed    = false;
    m_lastExpandedSize = sizeHint();

    constexpr int kRowH  = 25;
    constexpr int kSpinW = 64;

    // Fixed row heights
    set_fixed_row_height(ui->nameEdit, kRowH);
    set_fixed_row_height(ui->baseColorPickButton, kRowH);
    set_fixed_row_height(ui->baseColorSwatch, kRowH);
    set_fixed_row_height(ui->emissivePickButton, kRowH);
    set_fixed_row_height(ui->emissiveSwatch, kRowH);

    set_fixed_row_height(ui->metallicSlider, kRowH);
    set_fixed_row_height(ui->roughnessSlider, kRowH);
    set_fixed_row_height(ui->iorSlider, kRowH);
    set_fixed_row_height(ui->opacitySlider, kRowH);
    set_fixed_row_height(ui->emissiveIntensitySlider, kRowH);

    set_fixed_row_height(ui->metallicSpin, kRowH);
    set_fixed_row_height(ui->roughnessSpin, kRowH);
    set_fixed_row_height(ui->iorSpin, kRowH);
    set_fixed_row_height(ui->opacitySpin, kRowH);
    set_fixed_row_height(ui->emissiveIntensitySpin, kRowH);

    if (ui->metallicSpin)
    {
        ui->metallicSpin->setFixedWidth(kSpinW);
        ui->metallicSpin->setAlignment(Qt::AlignRight);
    }
    if (ui->roughnessSpin)
    {
        ui->roughnessSpin->setFixedWidth(kSpinW);
        ui->roughnessSpin->setAlignment(Qt::AlignRight);
    }
    if (ui->iorSpin)
    {
        ui->iorSpin->setFixedWidth(kSpinW);
        ui->iorSpin->setAlignment(Qt::AlignRight);
    }
    if (ui->opacitySpin)
    {
        ui->opacitySpin->setFixedWidth(kSpinW);
        ui->opacitySpin->setAlignment(Qt::AlignRight);
    }
    if (ui->emissiveIntensitySpin)
    {
        ui->emissiveIntensitySpin->setFixedWidth(kSpinW);
        ui->emissiveIntensitySpin->setAlignment(Qt::AlignRight);
    }

    set_fixed_row_height(ui->baseMapCombo, kRowH);
    set_fixed_row_height(ui->normalMapCombo, kRowH);
    set_fixed_row_height(ui->metallicMapCombo, kRowH);
    set_fixed_row_height(ui->roughnessMapCombo, kRowH);
    set_fixed_row_height(ui->aoMapCombo, kRowH);
    set_fixed_row_height(ui->emissiveMapCombo, kRowH);
    set_fixed_row_height(ui->mraoMapCombo, kRowH);

    tune_combo_popup(ui->baseMapCombo, 12);
    tune_combo_popup(ui->normalMapCombo, 12);
    tune_combo_popup(ui->metallicMapCombo, 12);
    tune_combo_popup(ui->roughnessMapCombo, 12);
    tune_combo_popup(ui->aoMapCombo, 12);
    tune_combo_popup(ui->emissiveMapCombo, 12);
    tune_combo_popup(ui->mraoMapCombo, 12);

    if (ui->materialList)
    {
        ui->materialList->setUniformItemSizes(true);
        ui->materialList->setSpacing(0);
        ui->materialList->setStyleSheet("QListWidget::item { height: 25px; }");
    }

    if (ui->rightPanel)
    {
        // Keep it stable: fixed 420 like your original.
        ui->rightPanel->setMinimumWidth(420);
        ui->rightPanel->setMaximumWidth(420);

        m_rightPanelMinW = ui->rightPanel->minimumWidth();
        m_rightPanelMaxW = ui->rightPanel->maximumWidth();
    }

    if (ui->propsGrid)
    {
        // 3 columns: label | slider | spin
        ui->propsGrid->setColumnMinimumWidth(0, 110);
        ui->propsGrid->setColumnStretch(0, 0);
        ui->propsGrid->setColumnStretch(1, 1);
        ui->propsGrid->setColumnStretch(2, 0);

        for (int r = 0; r <= 14; ++r)
            ui->propsGrid->setRowMinimumHeight(r, 25);
    }
}

MaterialEditorDialog::~MaterialEditorDialog() noexcept
{
    delete ui;
}

void MaterialEditorDialog::idleEvent(Core* core)
{
    m_core = core;

    MaterialEditor* ed = (m_core) ? m_core->materialEditor() : nullptr;
    if (!ed)
    {
        setUiEnabled(false);
        return;
    }

    rebuildMapCombosIfNeeded();

    const SysCounterPtr ctr = ed->changeCounter();
    const uint64_t      v   = ctr ? ctr->value() : 0;

    if (v != m_lastLibraryCounter)
    {
        m_lastLibraryCounter = v;
        refreshMaterialList();
    }

    const int32_t id = currentMaterialId();
    if (id >= 0)
    {
        const Material* m = ed->material(id);
        if (m)
        {
            const SysCounterPtr mctr = m->changeCounter();
            const uint64_t      mv   = mctr ? mctr->value() : 0;
            if (mv != m_lastMaterialCounter)
            {
                m_lastMaterialCounter = mv;
                loadMaterialToUi(id);
            }
        }
    }
}

void MaterialEditorDialog::setSpinSilently(QDoubleSpinBox* sp, double v) noexcept
{
    if (!sp)
        return;

    QSignalBlocker b(*sp);
    sp->setValue(v);
}

// ------------------------------------------------------------
// Combo helpers
// ------------------------------------------------------------

void MaterialEditorDialog::initMapCombos()
{
    auto initCombo = [this](QComboBox* cb) {
        if (!cb)
            return;

        QSignalBlocker block(*cb);
        cb->clear();
        cb->addItem("None", QVariant::fromValue<int>(kInvalidImageId));

        if (!m_core)
            return;

        ImageHandler* ih = m_core->imageHandler();
        if (!ih)
            return;

        const auto& imgs = ih->images();
        for (int32_t i = 0; i < int32_t(imgs.size()); ++i)
            cb->addItem(QString::fromStdString(imgs[size_t(i)].name()), QVariant::fromValue<int>(i));
    };

    initCombo(ui->baseMapCombo);
    initCombo(ui->normalMapCombo);
    initCombo(ui->metallicMapCombo);
    initCombo(ui->roughnessMapCombo);
    initCombo(ui->aoMapCombo);
    initCombo(ui->mraoMapCombo);
    initCombo(ui->emissiveMapCombo);
}

void MaterialEditorDialog::rebuildMapCombosIfNeeded()
{
    if (!m_core)
        return;

    ImageHandler* ih = m_core->imageHandler();
    if (!ih)
        return;

    const SysCounterPtr ctr = ih->changeCounter();
    const uint64_t      v   = ctr ? ctr->value() : 0;

    if (v == m_lastImagesCounter)
        return;

    m_lastImagesCounter = v;

    initMapCombos();

    const int32_t mid = currentMaterialId();
    if (mid >= 0)
        loadMaterialToUi(mid);
}

ImageId MaterialEditorDialog::comboImageId(QComboBox* cb) const noexcept
{
    if (!cb)
        return kInvalidImageId;

    return ImageId(cb->currentData().toInt());
}

void MaterialEditorDialog::setComboToImageId(QComboBox* cb, ImageId imageId) noexcept
{
    if (!cb)
        return;

    const int index = cb->findData(QVariant::fromValue<int>(int(imageId)));
    if (index >= 0)
        cb->setCurrentIndex(index);
    else
        cb->setCurrentIndex(0);
}

// ------------------------------------------------------------
// UI Helpers
// ------------------------------------------------------------

void MaterialEditorDialog::setUiEnabled(bool enabled)
{
    if (!ui)
        return;

    if (ui->propsFrame)
        ui->propsFrame->setEnabled(enabled);

    if (ui->assignButton)
        ui->assignButton->setEnabled(enabled);
}

int32_t MaterialEditorDialog::currentMaterialId() const noexcept
{
    if (!ui || !ui->materialList)
        return -1;

    QListWidgetItem* it = ui->materialList->currentItem();
    if (!it)
        return -1;

    return it->data(kRoleMaterialId).toInt();
}

Material* MaterialEditorDialog::currentMaterialMutable() noexcept
{
    if (!m_core)
        return nullptr;

    MaterialEditor* ed = m_core->materialEditor();
    if (!ed)
        return nullptr;

    const int32_t id = currentMaterialId();
    if (id < 0)
        return nullptr;

    return ed->material(id);
}

void MaterialEditorDialog::refreshMaterialList()
{
    if (!ui || !ui->materialList || !m_core)
        return;

    MaterialEditor* ed = m_core->materialEditor();
    if (!ed)
        return;

    const int32_t keepId      = currentMaterialId();
    int           rowToSelect = -1;

    {
        QSignalBlocker block(*ui->materialList);

        ui->materialList->clear();

        const std::vector<MaterialEditor::Entry> entries = ed->list();
        for (const auto& e : entries)
        {
            auto* item = new QListWidgetItem(QString::fromStdString(e.name));
            item->setData(kRoleMaterialId, e.id);
            ui->materialList->addItem(item);
        }

        if (keepId >= 0)
        {
            for (int i = 0; i < ui->materialList->count(); ++i)
            {
                QListWidgetItem* it = ui->materialList->item(i);
                if (it && it->data(kRoleMaterialId).toInt() == keepId)
                {
                    rowToSelect = i;
                    break;
                }
            }
        }

        if (rowToSelect < 0 && ui->materialList->count() > 0)
            rowToSelect = 0;
    }

    if (rowToSelect >= 0)
        ui->materialList->setCurrentRow(rowToSelect);

    const int32_t id = currentMaterialId();
    if (id >= 0)
        loadMaterialToUi(id);
}

void MaterialEditorDialog::loadMaterialToUi(int32_t id)
{
    if (!ui || !m_core)
        return;

    MaterialEditor* ed = m_core->materialEditor();
    if (!ed)
        return;

    const Material* m = ed->material(id);
    if (!m)
    {
        setUiEnabled(false);
        return;
    }

    setUiEnabled(true);

    const QSignalBlocker b0(ui->nameEdit);
    const QSignalBlocker b1(ui->metallicSlider);
    const QSignalBlocker b2(ui->roughnessSlider);
    const QSignalBlocker b3(ui->iorSlider);
    const QSignalBlocker b4(ui->opacitySlider);
    const QSignalBlocker b5(ui->emissiveIntensitySlider);

    const QSignalBlocker c0(ui->baseMapCombo);
    const QSignalBlocker c1(ui->normalMapCombo);
    const QSignalBlocker c2(ui->metallicMapCombo);
    const QSignalBlocker c3(ui->roughnessMapCombo);
    const QSignalBlocker c4(ui->aoMapCombo);
    const QSignalBlocker c5(ui->mraoMapCombo);
    const QSignalBlocker c6(ui->emissiveMapCombo);

    if (ui->nameEdit)
        ui->nameEdit->setText(QString::fromStdString(m->name()));

    // Metallic
    if (ui->metallicSlider)
    {
        const int sv = slider_from_float(m->metallic(), kMetallicRange.fmin, kMetallicRange.fmax, kMetallicRange.smin, kMetallicRange.smax);
        ui->metallicSlider->setValue(sv);
    }
    setSpinSilently(ui->metallicSpin, double(m->metallic()));

    // Roughness (perceptual)
    if (ui->roughnessSlider)
    {
        const float t  = roughness_to_perceptual(m->roughness());
        const int   sv = slider_from_01(t, kRoughnessUiRange.smin, kRoughnessUiRange.smax);
        ui->roughnessSlider->setValue(sv);
    }
    setSpinSilently(ui->roughnessSpin, double(m->roughness()));

    // IOR
    if (ui->iorSlider)
    {
        const int sv = slider_from_float(m->ior(), kIorRange.fmin, kIorRange.fmax, kIorRange.smin, kIorRange.smax);
        ui->iorSlider->setValue(sv);
    }
    setSpinSilently(ui->iorSpin, double(m->ior()));

    // Opacity
    if (ui->opacitySlider)
    {
        const int sv = slider_from_float(m->opacity(), kOpacityRange.fmin, kOpacityRange.fmax, kOpacityRange.smin, kOpacityRange.smax);
        ui->opacitySlider->setValue(sv);
    }
    setSpinSilently(ui->opacitySpin, double(m->opacity()));

    const float emissiveIntensity = m->emissiveIntensity();

    if (ui->emissiveIntensitySlider)
    {
        const int sv = slider_from_float(emissiveIntensity,
                                         kEmissiveIntRange.fmin,
                                         kEmissiveIntRange.fmax,
                                         kEmissiveIntRange.smin,
                                         kEmissiveIntRange.smax);
        ui->emissiveIntensitySlider->setValue(sv);
    }

    setSpinSilently(ui->emissiveIntensitySpin, double(emissiveIntensity));

    // Swatches
    if (ui->baseColorSwatch)
        set_swatch(ui->baseColorSwatch, to_qcolor(m->baseColor()));
    if (ui->emissiveSwatch)
        set_swatch(ui->emissiveSwatch, to_qcolor(m->emissiveColor()));

    // Textures (None = -1)
    setComboToImageId(ui->baseMapCombo, m->baseColorTexture());
    setComboToImageId(ui->normalMapCombo, m->normalTexture());
    setComboToImageId(ui->metallicMapCombo, m->metallicTexture());
    setComboToImageId(ui->roughnessMapCombo, m->roughnessTexture());
    setComboToImageId(ui->aoMapCombo, m->aoTexture());
    setComboToImageId(ui->emissiveMapCombo, m->emissiveTexture());

    // MRAO display-only (use if exists; otherwise keep None)
    // If you have mraoTexture(), show it; otherwise leave None.
    ImageId mraoId = kInvalidImageId;
    if constexpr (requires { m->mraoTexture(); })
        mraoId = m->mraoTexture();
    setComboToImageId(ui->mraoMapCombo, mraoId);

    const SysCounterPtr mctr = m->changeCounter();
    m_lastMaterialCounter    = mctr ? mctr->value() : 0;
}

// ------------------------------------------------------------
// Signals
// ------------------------------------------------------------

void MaterialEditorDialog::onMaterialSelectionChanged(QListWidgetItem* current, QListWidgetItem*)
{
    if (!current)
    {
        setUiEnabled(false);
        return;
    }

    const int32_t id = current->data(kRoleMaterialId).toInt();
    loadMaterialToUi(id);
}

void MaterialEditorDialog::onAssignClicked()
{
    if (!m_core)
        return;

    const int32_t id = currentMaterialId();
    if (id >= 0)
        m_core->assignMaterial(id);
}

void MaterialEditorDialog::onNameEdited()
{
    Material* m = currentMaterialMutable();
    if (!m || !ui || !ui->nameEdit)
        return;

    const QString qs = ui->nameEdit->text().trimmed();
    m->name(qs.toStdString());
}

// Slider -> material + spin sync
void MaterialEditorDialog::onMetallicChanged(int v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = slider_to_float(v, kMetallicRange.smin, kMetallicRange.smax, kMetallicRange.fmin, kMetallicRange.fmax);
    m->metallic(x);

    m_blockSpinSignals = true;
    setSpinSilently(ui->metallicSpin, double(x));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onRoughnessChanged(int v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float t = slider_to_01(v, kRoughnessUiRange.smin, kRoughnessUiRange.smax);
    const float r = perceptual_to_roughness(t);
    m->roughness(r);

    m_blockSpinSignals = true;
    setSpinSilently(ui->roughnessSpin, double(r));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onIorChanged(int v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float ior = slider_to_float(v, kIorRange.smin, kIorRange.smax, kIorRange.fmin, kIorRange.fmax);
    m->ior(ior);

    m_blockSpinSignals = true;
    setSpinSilently(ui->iorSpin, double(ior));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onOpacityChanged(int v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = slider_to_float(v, kOpacityRange.smin, kOpacityRange.smax, kOpacityRange.fmin, kOpacityRange.fmax);
    m->opacity(x);

    m_blockSpinSignals = true;
    setSpinSilently(ui->opacitySpin, double(x));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onEmissiveIntensityChanged(int v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = slider_to_float(v, kEmissiveIntRange.smin, kEmissiveIntRange.smax, kEmissiveIntRange.fmin, kEmissiveIntRange.fmax);

    m->emissiveIntensity(x);

    m_blockSpinSignals = true;
    setSpinSilently(ui->emissiveIntensitySpin, double(x));
    m_blockSpinSignals = false;
}

// Spin -> material + slider sync
void MaterialEditorDialog::onMetallicSpinChanged(double v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = clamp_float(float(v), 0.0f, 1.0f);
    m->metallic(x);

    m_blockSpinSignals = true;
    if (ui->metallicSlider)
        ui->metallicSlider->setValue(slider_from_float(x, kMetallicRange.fmin, kMetallicRange.fmax, kMetallicRange.smin, kMetallicRange.smax));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onRoughnessSpinChanged(double v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float r = clamp_float(float(v), 0.0f, 1.0f);
    m->roughness(r);

    m_blockSpinSignals = true;
    if (ui->roughnessSlider)
    {
        const float t = roughness_to_perceptual(r);
        ui->roughnessSlider->setValue(slider_from_01(t, kRoughnessUiRange.smin, kRoughnessUiRange.smax));
    }
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onIorSpinChanged(double v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = clamp_float(float(v), 1.0f, 3.0f);
    m->ior(x);

    m_blockSpinSignals = true;
    if (ui->iorSlider)
        ui->iorSlider->setValue(slider_from_float(x, kIorRange.fmin, kIorRange.fmax, kIorRange.smin, kIorRange.smax));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onOpacitySpinChanged(double v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = clamp_float(float(v), 0.0f, 1.0f);
    m->opacity(x);

    m_blockSpinSignals = true;
    if (ui->opacitySlider)
        ui->opacitySlider->setValue(slider_from_float(x, kOpacityRange.fmin, kOpacityRange.fmax, kOpacityRange.smin, kOpacityRange.smax));
    m_blockSpinSignals = false;
}

void MaterialEditorDialog::onEmissiveIntensitySpinChanged(double v)
{
    if (m_blockSpinSignals)
        return;

    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = clamp_float(float(v), 0.0f, 2.0f);

    m->emissiveIntensity(x);

    m_blockSpinSignals = true;
    if (ui->emissiveIntensitySlider)
        ui->emissiveIntensitySlider->setValue(slider_from_float(x, kEmissiveIntRange.fmin, kEmissiveIntRange.fmax, kEmissiveIntRange.smin, kEmissiveIntRange.smax));
    m_blockSpinSignals = false;
}

// ------------------------------------------------------------
// Color pickers
// ------------------------------------------------------------

void MaterialEditorDialog::onPickBaseColor()
{
    Material* m = currentMaterialMutable();
    if (!m || !ui || !ui->baseColorSwatch)
        return;

    const QColor start = to_qcolor(m->baseColor());
    const QColor c     = QColorDialog::getColor(start, this, "Base Color");
    if (!c.isValid())
        return;

    m->baseColor(from_qcolor(c));
    set_swatch(ui->baseColorSwatch, c);
}

void MaterialEditorDialog::onPickEmissive()
{
    Material* m = currentMaterialMutable();
    if (!m || !ui || !ui->emissiveSwatch)
        return;

    const QColor start = to_qcolor(m->emissiveColor());
    const QColor c     = QColorDialog::getColor(start, this, "Emissive Color");
    if (!c.isValid())
        return;

    m->emissiveColor(from_qcolor(c));

    set_swatch(ui->emissiveSwatch, c);
}

// ------------------------------------------------------------
// Maps
// ------------------------------------------------------------

void MaterialEditorDialog::onBaseMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    m->baseColorTexture(comboImageId(ui->baseMapCombo));
}

void MaterialEditorDialog::onNormalMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    m->normalTexture(comboImageId(ui->normalMapCombo));
}

void MaterialEditorDialog::onMetallicMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    m->metallicTexture(comboImageId(ui->metallicMapCombo));
}

void MaterialEditorDialog::onRoughnessMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    m->roughnessTexture(comboImageId(ui->roughnessMapCombo));
}

void MaterialEditorDialog::onAoMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    m->aoTexture(comboImageId(ui->aoMapCombo));
}

void MaterialEditorDialog::onEmissiveMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    m->emissiveTexture(comboImageId(ui->emissiveMapCombo));
}

// ------------------------------------------------------------
// Collapse/Expand
// ------------------------------------------------------------

void MaterialEditorDialog::onToggleLeft()
{
    applyCollapsedState(!m_leftCollapsed);
}

void MaterialEditorDialog::applyCollapsedState(bool collapsed, bool force)
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

        // Keep right panel fixed-width even when left is hidden (old behavior).
        ui->rightPanel->setMinimumWidth(m_rightPanelMinW);
        ui->rightPanel->setMaximumWidth(m_rightPanelMaxW);
        ui->rightPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        // Force splitter to give everything to the right panel.
        const int rightFixedW = ui->rightPanel->maximumWidth();

        QList<int> sizes;
        sizes.reserve(ui->splitterMain->count());
        for (int i = 0; i < ui->splitterMain->count(); ++i)
        {
            if (i == m_leftIndex)
                sizes.push_back(0);
            else if (i == m_rightIndex)
                sizes.push_back(rightFixedW);
            else
                sizes.push_back(0);
        }
        ui->splitterMain->setSizes(sizes);

        // Snap dialog width to exactly the right panel width (+ margins).
        const int marginW =
            (layout() ? (layout()->contentsMargins().left() + layout()->contentsMargins().right()) : 0) +
            (ui->splitterMain ? (ui->splitterMain->contentsMargins().left() + ui->splitterMain->contentsMargins().right()) : 0);

        const int newW = rightFixedW + marginW + 2;

        setMinimumWidth(newW);
        setMaximumWidth(newW);
        resize(newW, height());
    }
    else
    {
        ui->leftPanel->setVisible(true);

        ui->rightPanel->setMinimumWidth(m_rightPanelMinW);
        ui->rightPanel->setMaximumWidth(m_rightPanelMaxW);
        ui->rightPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        // Restore expanded minimum so we don't get "pic1".
        setMinimumSize(m_expandedMinSize);
        setMaximumSize(m_expandedMaxSize);

        const int rightW = ui->rightPanel->maximumWidth();

        if (m_lastExpandedSize.width() < rightW + 220)
            m_lastExpandedSize.setWidth(rightW + 340);
        if (m_lastExpandedSize.height() < minimumHeight())
            m_lastExpandedSize.setHeight(minimumHeight());

        resize(m_lastExpandedSize);

        const int rightFixedW = ui->rightPanel->maximumWidth();
        const int leftW       = std::max(0, width() - rightFixedW);

        QList<int> sizes;
        sizes.reserve(ui->splitterMain->count());
        for (int i = 0; i < ui->splitterMain->count(); ++i)
        {
            if (i == m_leftIndex)
                sizes.push_back(leftW);
            else if (i == m_rightIndex)
                sizes.push_back(rightFixedW);
            else
                sizes.push_back(0);
        }
        ui->splitterMain->setSizes(sizes);
    }
}
