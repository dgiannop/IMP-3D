//============================================================
// MaterialEditorDialog.cpp  (FULL REPLACEMENT)
//============================================================
#include "MaterialEditorDialog.hpp"

#include <QAbstractButton>
#include <QColorDialog>
#include <QComboBox>
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
    static constexpr int kRoleImageId    = Qt::UserRole + 2;

    // -----------------------------
    // Slider mapping helpers
    // -----------------------------
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

        v = clamp_int(v, smin, smax);

        const float t = (static_cast<float>(v - smin)) / static_cast<float>(smax - smin);
        return fmin + (fmax - fmin) * t;
    }

    static int slider_from_float(float x, float fmin, float fmax, int smin, int smax) noexcept
    {
        if (smax <= smin)
            return smin;

        x = clamp_float(x, fmin, fmax);

        const float t  = (x - fmin) / (fmax - fmin);
        const float sv = static_cast<float>(smin) + t * static_cast<float>(smax - smin);

        return clamp_int(static_cast<int>(sv + 0.5f), smin, smax);
    }

    static float slider_to_01(int v, int smin, int smax) noexcept
    {
        return slider_to_float(v, smin, smax, 0.0f, 1.0f);
    }

    static int slider_from_01(float t, int smin, int smax) noexcept
    {
        return slider_from_float(t, 0.0f, 1.0f, smin, smax);
    }

    // -----------------------------
    // Roughness perceptual mapping
    // -----------------------------
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

    // -----------------------------
    // Colors
    // -----------------------------
    static QColor to_qcolor(const glm::vec3& c) noexcept
    {
        const int r = static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        const int g = static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        const int b = static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        return QColor(r, g, b);
    }

    static glm::vec3 from_qcolor(const QColor& c) noexcept
    {
        return glm::vec3(
            static_cast<float>(c.red()) / 255.0f,
            static_cast<float>(c.green()) / 255.0f,
            static_cast<float>(c.blue()) / 255.0f);
    }

    static void set_button_swatch(QWidget* w, const QColor& c)
    {
        if (!w)
            return;

        const QString css = QString("background-color: %1; border: 1px solid rgba(255,255,255,40);")
                                .arg(c.name(QColor::HexRgb));
        w->setStyleSheet(css);
    }

    // -----------------------------
    // Slider ranges
    // -----------------------------
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

} // namespace

// ------------------------------------------------------------

MaterialEditorDialog::MaterialEditorDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::MaterialEditorDialog)
{
    ui->setupUi(this);

    setWindowTitle("Material Editor");

    setMinimumSize(420, 150);
    setMaximumSize(900, 700);

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

    if (ui->toggleLeftButton)
        connect(ui->toggleLeftButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onToggleLeft);

    if (ui->materialList)
        connect(ui->materialList, &QListWidget::currentItemChanged, this, &MaterialEditorDialog::onMaterialSelectionChanged);

    if (ui->assignButton)
        connect(ui->assignButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onAssignClicked);

    if (ui->nameEdit)
        connect(ui->nameEdit, &QLineEdit::editingFinished, this, &MaterialEditorDialog::onNameEdited);

    if (ui->metallicSlider)
        connect(ui->metallicSlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onMetallicChanged);

    if (ui->roughnessSlider)
        connect(ui->roughnessSlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onRoughnessChanged);

    if (ui->iorSlider)
        connect(ui->iorSlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onIorChanged);

    if (ui->opacitySlider)
        connect(ui->opacitySlider, &QSlider::valueChanged, this, &MaterialEditorDialog::onOpacityChanged);

    if (ui->baseColorPickButton)
        connect(ui->baseColorPickButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onPickBaseColor);

    if (ui->emissivePickButton)
        connect(ui->emissivePickButton, &QAbstractButton::clicked, this, &MaterialEditorDialog::onPickEmissive);

    initMapCombos();

    if (ui->baseMapCombo)
        connect(ui->baseMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onBaseMapChanged);
    if (ui->normalMapCombo)
        connect(ui->normalMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onNormalMapChanged);
    if (ui->metallicMapCombo)
        connect(ui->metallicMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onMraoMapChanged);
    if (ui->roughnessMapCombo)
        connect(ui->roughnessMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onMraoMapChanged);
    if (ui->aoMapCombo)
        connect(ui->aoMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onMraoMapChanged);
    if (ui->emissiveMapCombo)
        connect(ui->emissiveMapCombo, &QComboBox::currentIndexChanged, this, &MaterialEditorDialog::onEmissiveMapChanged);

    m_leftCollapsed    = false;
    m_lastExpandedSize = sizeHint();

    if (ui->propsFrame)
    {
        for (QWidget* w : ui->propsFrame->findChildren<QWidget*>())
        {
            w->setFixedHeight(25);
        }
    }

    if (ui->materialList)
    {
        ui->materialList->setUniformItemSizes(true);
        ui->materialList->setSpacing(0);
        ui->materialList->setStyleSheet("QListWidget::item { height: 25px; }");
    }

    if (ui->rightPanel)
    {
        ui->rightPanel->setMinimumWidth(300);
        ui->rightPanel->setMaximumWidth(300);

        m_rightPanelMinW = ui->rightPanel->minimumWidth();
        m_rightPanelMaxW = ui->rightPanel->maximumWidth();
    }

    if (ui->propsGrid)
    {
        ui->propsGrid->setColumnMinimumWidth(110, 0);
        ui->propsGrid->setColumnStretch(0, 0);
        ui->propsGrid->setColumnStretch(1, 1);

        for (int r = 0; r <= 12; ++r)
            ui->propsGrid->setRowMinimumHeight(r, 24);
    }
}

MaterialEditorDialog::~MaterialEditorDialog() noexcept
{
    delete ui;
}

// ------------------------------------------------------------

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

// ------------------------------------------------------------
// Combo helpers
// ------------------------------------------------------------

void MaterialEditorDialog::initMapCombos()
{
    // Start with "None" so the UI is usable before Core is set.
    auto initNone = [](QComboBox* cb) {
        if (!cb)
            return;
        QSignalBlocker b(*cb);
        cb->clear();
        cb->addItem("None", QVariant::fromValue<int>(static_cast<int>(kInvalidImageId)));
    };

    initNone(ui->baseMapCombo);
    initNone(ui->metallicMapCombo);
    initNone(ui->roughnessMapCombo);
    initNone(ui->normalMapCombo);
    initNone(ui->aoMapCombo);
    initNone(ui->emissiveMapCombo);
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

    fillImageCombo(ui->baseMapCombo);
    fillImageCombo(ui->normalMapCombo);
    fillImageCombo(ui->metallicMapCombo);
    fillImageCombo(ui->roughnessMapCombo);
    fillImageCombo(ui->aoMapCombo);
    fillImageCombo(ui->emissiveMapCombo);

    // Re-apply current material selections into the now-refreshed combos.
    const int32_t mid = currentMaterialId();
    if (mid >= 0)
        loadMaterialToUi(mid);
}

void MaterialEditorDialog::fillImageCombo(QComboBox* cb)
{
    if (!cb || !m_core)
        return;

    ImageHandler* ih = m_core->imageHandler();
    if (!ih)
        return;

    const auto& imgs = ih->images();

    QSignalBlocker b(*cb);
    cb->clear();

    cb->addItem("None", QVariant::fromValue<int>(static_cast<int>(kInvalidImageId)));

    for (int32_t i = 0; i < static_cast<int32_t>(imgs.size()); ++i)
    {
        const Image&      img = imgs[static_cast<size_t>(i)];
        const std::string nm  = img.name();

        QString label;
        if (!nm.empty())
            label = QString::fromStdString(nm);
        else
            label = QString("Image %1").arg(i);

        cb->addItem(label, QVariant::fromValue<int>(i));
    }
}

void MaterialEditorDialog::setComboToImageId(QComboBox* cb, ImageId imageId)
{
    if (!cb)
        return;

    const int want = static_cast<int>(imageId);

    for (int i = 0; i < cb->count(); ++i)
    {
        const int v = cb->itemData(i).toInt();
        if (v == want)
        {
            cb->setCurrentIndex(i);
            return;
        }
    }

    // If the image id no longer exists, fall back to None.
    cb->setCurrentIndex(0);
}

ImageId MaterialEditorDialog::comboImageId(const QComboBox* cb) const noexcept
{
    if (!cb)
        return kInvalidImageId;

    const int idx = cb->currentIndex();
    if (idx < 0)
        return kInvalidImageId;

    return static_cast<ImageId>(cb->itemData(idx).toInt());
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
    const QSignalBlocker b5(ui->baseMapCombo);
    const QSignalBlocker b6(ui->normalMapCombo);
    const QSignalBlocker b7(ui->metallicMapCombo);
    const QSignalBlocker b8(ui->roughnessMapCombo);
    const QSignalBlocker b9(ui->aoMapCombo);
    const QSignalBlocker b10(ui->emissiveMapCombo);

    if (ui->nameEdit)
        ui->nameEdit->setText(QString::fromStdString(m->name()));

    if (ui->metallicSlider)
    {
        const int sv = slider_from_float(m->metallic(),
                                         kMetallicRange.fmin,
                                         kMetallicRange.fmax,
                                         kMetallicRange.smin,
                                         kMetallicRange.smax);
        ui->metallicSlider->setValue(sv);
    }

    if (ui->roughnessSlider)
    {
        const float t  = roughness_to_perceptual(m->roughness());
        const int   sv = slider_from_01(t, kRoughnessUiRange.smin, kRoughnessUiRange.smax);
        ui->roughnessSlider->setValue(sv);
    }

    if (ui->iorSlider)
    {
        const int sv = slider_from_float(m->ior(),
                                         kIorRange.fmin,
                                         kIorRange.fmax,
                                         kIorRange.smin,
                                         kIorRange.smax);
        ui->iorSlider->setValue(sv);
    }

    if (ui->opacitySlider)
    {
        const int sv = slider_from_float(m->opacity(),
                                         kOpacityRange.fmin,
                                         kOpacityRange.fmax,
                                         kOpacityRange.smin,
                                         kOpacityRange.smax);
        ui->opacitySlider->setValue(sv);
    }

    if (ui->baseColorPreviewButton)
        set_button_swatch(ui->baseColorPreviewButton, to_qcolor(m->baseColor()));

    if (ui->emissivePreviewButton)
        set_button_swatch(ui->emissivePreviewButton, to_qcolor(m->emissiveColor()));

    // Textures
    if (ui->baseMapCombo)
        setComboToImageId(ui->baseMapCombo, m->baseColorTexture());

    if (ui->normalMapCombo)
        setComboToImageId(ui->normalMapCombo, m->normalTexture());

    // MRAO is a single slot in Material, so keep the three combos in sync.
    if (ui->metallicMapCombo)
        setComboToImageId(ui->metallicMapCombo, m->mraoTexture());
    if (ui->roughnessMapCombo)
        setComboToImageId(ui->roughnessMapCombo, m->mraoTexture());
    if (ui->aoMapCombo)
        setComboToImageId(ui->aoMapCombo, m->mraoTexture());

    if (ui->emissiveMapCombo)
        setComboToImageId(ui->emissiveMapCombo, m->emissiveTexture());

    const SysCounterPtr mctr = m->changeCounter();
    m_lastMaterialCounter    = mctr ? mctr->value() : 0;
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

// ------------------------------------------------------------
// Signals
// ------------------------------------------------------------

void MaterialEditorDialog::onMaterialSelectionChanged(QListWidgetItem* current, QListWidgetItem* /*previous*/)
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

void MaterialEditorDialog::onMetallicChanged(int v)
{
    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = slider_to_float(v,
                                    kMetallicRange.smin,
                                    kMetallicRange.smax,
                                    kMetallicRange.fmin,
                                    kMetallicRange.fmax);
    m->metallic(x);
}

void MaterialEditorDialog::onRoughnessChanged(int v)
{
    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float t = slider_to_01(v, kRoughnessUiRange.smin, kRoughnessUiRange.smax);
    const float r = perceptual_to_roughness(t);

    m->roughness(r);
}

void MaterialEditorDialog::onIorChanged(int v)
{
    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float ior = slider_to_float(v,
                                      kIorRange.smin,
                                      kIorRange.smax,
                                      kIorRange.fmin,
                                      kIorRange.fmax);
    m->ior(ior);
}

void MaterialEditorDialog::onOpacityChanged(int v)
{
    Material* m = currentMaterialMutable();
    if (!m)
        return;

    const float x = slider_to_float(v,
                                    kOpacityRange.smin,
                                    kOpacityRange.smax,
                                    kOpacityRange.fmin,
                                    kOpacityRange.fmax);
    m->opacity(x);
}

void MaterialEditorDialog::onPickBaseColor()
{
    Material* m = currentMaterialMutable();
    if (!m || !ui || !ui->baseColorPreviewButton)
        return;

    const QColor start = to_qcolor(m->baseColor());
    const QColor c     = QColorDialog::getColor(start, this, "Base Color");
    if (!c.isValid())
        return;

    const glm::vec3 col = from_qcolor(c);
    m->baseColor(col);
    set_button_swatch(ui->baseColorPreviewButton, c);
}

void MaterialEditorDialog::onPickEmissive()
{
    Material* m = currentMaterialMutable();
    if (!m || !ui || !ui->emissivePreviewButton)
        return;

    const QColor start = to_qcolor(m->emissiveColor());
    const QColor c     = QColorDialog::getColor(start, this, "Emissive Color");
    if (!c.isValid())
        return;

    const glm::vec3 col = from_qcolor(c);
    m->emissiveColor(col);
    set_button_swatch(ui->emissivePreviewButton, c);
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

void MaterialEditorDialog::onMraoMapChanged(int)
{
    Material* m = currentMaterialMutable();
    if (!m || !ui)
        return;

    // All three UI combos represent the single Material::mraoTexture() slot.
    // Choose the combo that triggered the signal by taking the first non-null
    // and syncing all to the new value.
    const ImageId idM = comboImageId(ui->metallicMapCombo);
    const ImageId idR = comboImageId(ui->roughnessMapCombo);
    const ImageId idA = comboImageId(ui->aoMapCombo);

    ImageId chosen = idM;
    if (chosen == kInvalidImageId)
        chosen = idR;
    if (chosen == kInvalidImageId)
        chosen = idA;

    m->mraoTexture(chosen);

    // Keep the UI consistent.
    {
        const QSignalBlocker b0(ui->metallicMapCombo);
        const QSignalBlocker b1(ui->roughnessMapCombo);
        const QSignalBlocker b2(ui->aoMapCombo);

        setComboToImageId(ui->metallicMapCombo, chosen);
        setComboToImageId(ui->roughnessMapCombo, chosen);
        setComboToImageId(ui->aoMapCombo, chosen);
    }
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
