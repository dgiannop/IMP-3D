#include "MaterialAssignDialog.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QPushButton>
#include <QStringListModel>

#include "Core.hpp"
#include "Material.hpp"
#include "MaterialEditor.hpp"
#include "SubWindows/ui_MaterialAssignDialog.h"

namespace
{
    static uint64_t counter_stamp(const SysCounterPtr& c) noexcept
    {
        return c ? c->value() : 0ull;
    }

    static QColor to_qcolor(const glm::vec3& v) noexcept
    {
        const int r = std::clamp(static_cast<int>(v.x * 255.f + 0.5f), 0, 255);
        const int g = std::clamp(static_cast<int>(v.y * 255.f + 0.5f), 0, 255);
        const int b = std::clamp(static_cast<int>(v.z * 255.f + 0.5f), 0, 255);
        return QColor(r, g, b);
    }

    static glm::vec3 to_vec3(const QColor& c) noexcept
    {
        return glm::vec3(
            static_cast<float>(c.red()) / 255.f,
            static_cast<float>(c.green()) / 255.f,
            static_cast<float>(c.blue()) / 255.f);
    }

} // namespace

MaterialAssignDialog::MaterialAssignDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::MaterialAssignDialog)
{
    ui->setupUi(this);

    setWindowTitle("Assign Material");
    resize(460, 170);
    setMinimumSize(420, 150);
    setMaximumSize(640, 220);

    if (ui->titleLabel)
        ui->titleLabel->setText("Assign Material");

    ui->materialCombo->setEditable(true);
    ui->materialCombo->setInsertPolicy(QComboBox::NoInsert);

    m_model     = new QStringListModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);

    if (QLineEdit* edit = ui->materialCombo->lineEdit())
    {
        edit->setCompleter(m_completer);
        connect(edit, &QLineEdit::textEdited, this, &MaterialAssignDialog::onNameEdited);
    }

    connect(ui->pickColorButton, &QPushButton::clicked, this, &MaterialAssignDialog::onPickColor);
    connect(ui->colorSwatchButton, &QPushButton::clicked, this, &MaterialAssignDialog::onPickColor);

    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->assignButton, &QPushButton::clicked, this, &MaterialAssignDialog::onAssign);

    // When user selects an item from the dropdown (mouse/keyboard)
    connect(ui->materialCombo,
            QOverload<int>::of(&QComboBox::activated),
            this,
            &MaterialAssignDialog::onComboChanged);

    // Also handle programmatic index changes / keyboard nav
    connect(ui->materialCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &MaterialAssignDialog::onComboChanged);

    applySwatchColor(m_baseColor);
}

MaterialAssignDialog::~MaterialAssignDialog() noexcept
{
    delete ui;
}

void MaterialAssignDialog::idleEvent(Core* core)
{
    if (!core)
        return;

    m_core = core;

    MaterialEditor* me = core->materialEditor();
    if (!me)
        return;

    const SysCounterPtr cc = me->changeCounter();
    const uint64_t      st = counter_stamp(cc);

    // Rebuild only when needed
    if (!m_hasInitialList || cc != m_lastMatCounter || st != m_lastMatStamp)
    {
        m_lastMatCounter = cc;
        m_lastMatStamp   = st;
        m_hasInitialList = true;

        rebuildMaterialList(core);
    }
}

void MaterialAssignDialog::rebuildMaterialList(Core* core)
{
    MaterialEditor* me = core ? core->materialEditor() : nullptr;
    if (!me)
        return;

    const QString prevText = ui->materialCombo->currentText();

    m_entries.clear();

    ui->materialCombo->blockSignals(true);
    ui->materialCombo->clear();

    QStringList names;

    const auto list = me->list();
    m_entries.reserve(list.size());

    for (const auto& e : list)
    {
        MaterialEntry ent = {};
        ent.id            = e.id;
        ent.name          = QString::fromStdString(e.name);

        if (const Material* m = me->material(e.id))
        {
            ent.baseColor = to_qcolor(m->baseColor());
        }
        else
        {
            ent.baseColor = QColor(128, 128, 128);
        }

        m_entries.push_back(ent);
    }

    for (const MaterialEntry& e : m_entries)
    {
        ui->materialCombo->addItem(e.name);
        names.push_back(e.name);
    }

    m_model->setStringList(names);

    // Keep user text stable
    ui->materialCombo->setCurrentText(prevText);

    ui->materialCombo->blockSignals(false);

    // Sync swatch if current text matches an existing material
    onNameEdited(ui->materialCombo->currentText());
}

int32_t MaterialAssignDialog::findMaterialByName(const QString& name) const noexcept
{
    const QString key = name.trimmed();
    if (key.isEmpty())
        return -1;

    for (const MaterialEntry& e : m_entries)
    {
        if (QString::compare(e.name, key, Qt::CaseInsensitive) == 0)
            return e.id;
    }

    return -1;
}

void MaterialAssignDialog::onNameEdited(const QString& text)
{
    const int32_t id = findMaterialByName(text);

    if (id < 0)
    {
        // New name: allow picking a custom color.
        ui->pickColorButton->setEnabled(true);
        ui->colorSwatchButton->setEnabled(true);
        return;
    }

    // Existing material: load its base color
    for (const MaterialEntry& e : m_entries)
    {
        if (e.id == id)
        {
            m_baseColor = e.baseColor;
            applySwatchColor(m_baseColor);
            break;
        }
    }

    // Prevent editing base color here (since it's "assign", not "edit")
    ui->pickColorButton->setEnabled(false);
    ui->colorSwatchButton->setEnabled(false);
}

void MaterialAssignDialog::onComboChanged(int /*index*/)
{
    // Use the combo's current text (works for both selecting & typing)
    onNameEdited(ui->materialCombo->currentText());
}

void MaterialAssignDialog::onPickColor()
{
    QColorDialog dlg(m_baseColor, this);
    dlg.setOption(QColorDialog::ShowAlphaChannel, false);
    dlg.setOption(QColorDialog::DontUseNativeDialog, false); // try true if native is weird

    // Optional: place it centered over the parent (no explicit size)
    dlg.move(this->frameGeometry().center() - dlg.rect().center());

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QColor chosen = dlg.currentColor();
    if (!chosen.isValid())
        return;

    m_baseColor = chosen;
    applySwatchColor(m_baseColor);
}

void MaterialAssignDialog::applySwatchColor(const QColor& c)
{
    const QString css =
        QString("QPushButton { border: 1px solid #1f2228; background-color: rgb(%1,%2,%3); }")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue());

    ui->colorSwatchButton->setStyleSheet(css);
}

void MaterialAssignDialog::onAssign()
{
    if (!m_core)
    {
        reject();
        return;
    }

    MaterialEditor* me = m_core->materialEditor();
    if (!me)
    {
        reject();
        return;
    }

    const QString qname = ui->materialCombo->currentText().trimmed();
    if (qname.isEmpty())
        return;

    const std::string name = qname.toStdString();

    // Track if it existed before (so we can choose whether to overwrite baseColor)
    const int32_t existingId = findMaterialByName(qname);

    // Create or get
    const int32_t matId = me->createOrGet(name);
    if (matId < 0)
        return;

    // If new material, apply the chosen base color.
    // (If we want "always apply", remove the existingId check.)
    if (existingId < 0)
    {
        if (Material* m = me->material(matId))
        {
            m->baseColor(to_vec3(m_baseColor));
        }
    }

    m_core->assignMaterial(matId);

    accept();
}
