//============================================================
// CreateLightDialog.cpp
//============================================================
#include "CreateLightDialog.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>

#include "Core.hpp"
#include "SubWindows/ui_CreateLightDialog.h"

namespace
{
    static LightType light_type_from_text(const QString& s) noexcept
    {
        const QString t = s.trimmed();

        if (QString::compare(t, "Directional", Qt::CaseInsensitive) == 0)
            return LightType::Directional;

        if (QString::compare(t, "Point", Qt::CaseInsensitive) == 0)
            return LightType::Point;

        if (QString::compare(t, "Spot", Qt::CaseInsensitive) == 0)
            return LightType::Spot;

        return LightType::Directional;
    }

    static QString default_name_for_type_label(const QString& typeLabel) noexcept
    {
        const QString t = typeLabel.trimmed();
        if (t.isEmpty())
            return QString("Light");

        return t + " Light";
    }

    /**
     * @brief Checks whether the provided name matches any default "<Type> Light" pattern.
     *
     * This allows the dialog to keep the auto-generated name in sync with the selected type
     * as long as the user has not provided a custom name.
     */
    static bool is_default_type_name(const QString& name) noexcept
    {
        const QString n = name.trimmed();

        if (QString::compare(n, "Directional Light", Qt::CaseInsensitive) == 0)
            return true;

        if (QString::compare(n, "Point Light", Qt::CaseInsensitive) == 0)
            return true;

        if (QString::compare(n, "Spot Light", Qt::CaseInsensitive) == 0)
            return true;

        return false;
    }

    /**
     * @brief Determines if the name field should be auto-updated when type changes.
     *
     * Auto-update occurs when:
     *  - the name is empty, or
     *  - the name is currently one of the default "<Type> Light" values.
     */
    static bool should_autoupdate_name(const QString& currentName) noexcept
    {
        const QString n = currentName.trimmed();
        return n.isEmpty() || is_default_type_name(n);
    }

} // namespace

CreateLightDialog::CreateLightDialog(QWidget* parent) : SubWindowBase(parent), ui(new Ui::CreateLightDialog)
{
    ui->setupUi(this);

    setWindowTitle("Create Light");

    if (ui->createButton)
        connect(ui->createButton, &QAbstractButton::clicked, this, &CreateLightDialog::onCreate);

    if (ui->cancelButton)
        connect(ui->cancelButton, &QAbstractButton::clicked, this, &CreateLightDialog::onCancel);

    // Optional: pressing Enter in the name field triggers create.
    if (ui->nameEdit)
        connect(ui->nameEdit, &QLineEdit::returnPressed, this, &CreateLightDialog::onCreate);

    // Keep the default name in sync with the selected type as long as the user has not
    // provided a custom name.
    if (ui->typeCombo && ui->nameEdit)
    {
        connect(ui->typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            const QString curName = ui->nameEdit->text();
            if (!should_autoupdate_name(curName))
                return;

            const QString typeLabel = ui->typeCombo->currentText();
            ui->nameEdit->setText(default_name_for_type_label(typeLabel));
            ui->nameEdit->selectAll();
        });
    }

    // Initialize the name field to match the default selected type.
    // This runs once at construction time and avoids an empty name on first open.
    if (ui->typeCombo && ui->nameEdit)
    {
        const QString curName = ui->nameEdit->text();
        if (should_autoupdate_name(curName))
        {
            const QString typeLabel = ui->typeCombo->currentText();
            ui->nameEdit->setText(default_name_for_type_label(typeLabel));
            ui->nameEdit->selectAll();
        }
    }
}

CreateLightDialog::~CreateLightDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void CreateLightDialog::idleEvent(Core* core)
{
    m_core = core;
}

void CreateLightDialog::onCreate()
{
    if (!m_core)
    {
        reject();
        return;
    }

    if (!ui->typeCombo || !ui->nameEdit)
        return;

    const QString   typeLabel = ui->typeCombo->currentText().trimmed();
    const LightType type      = light_type_from_text(typeLabel);

    QString qname = ui->nameEdit->text().trimmed();
    if (qname.isEmpty())
        qname = default_name_for_type_label(typeLabel);

    const std::string name = qname.toStdString();

    // Light creation returns the created SceneLight*. The result can be ignored
    // by the dialog, but [[nodiscard]] should be acknowledged explicitly.
    [[maybe_unused]] SceneLight* created = m_core->createLight(name, type);

    accept();
}

void CreateLightDialog::onCancel()
{
    reject();
}
