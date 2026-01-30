//============================================================
// CreateLightDialog.cpp
//============================================================
#include "CreateLightDialog.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QShowEvent>
#include <QString>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "Core.hpp"
#include "SceneLight.hpp"
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

    static QString base_name_for_type_label(const QString& typeLabel) noexcept
    {
        const QString t = typeLabel.trimmed();
        if (t.isEmpty())
            return QString("Light");

        return t + " Light";
    }

    static bool is_default_type_name(const QString& name) noexcept
    {
        const QString n = name.trimmed();

        if (QString::compare(n, "Directional Light", Qt::CaseInsensitive) == 0)
            return true;

        if (QString::compare(n, "Point Light", Qt::CaseInsensitive) == 0)
            return true;

        if (QString::compare(n, "Spot Light", Qt::CaseInsensitive) == 0)
            return true;

        // Also treat suffixed defaults as "default", because they should track type too:
        // Directional Light.001 etc.
        const QString lower = n.toLower();
        if (lower.startsWith("directional light.") || lower.startsWith("point light.") ||
            lower.startsWith("spot light."))
        {
            return true;
        }

        return false;
    }

    static bool should_autoupdate_name(const QString& currentName) noexcept
    {
        const QString n = currentName.trimmed();
        return n.isEmpty() || is_default_type_name(n);
    }

    static bool equals_ci(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
        {
            const unsigned char ca = static_cast<unsigned char>(a[i]);
            const unsigned char cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb))
                return false;
        }
        return true;
    }

    static bool name_exists(const std::vector<SceneLight*>& lights, const std::string& name) noexcept
    {
        for (const SceneLight* l : lights)
        {
            if (!l)
                continue;

            // Assumes SceneLight::name() returns std::string_view (or similar)
            if (equals_ci(l->name(), name))
                return true;
        }
        return false;
    }

    static std::string make_unique_name(const std::vector<SceneLight*>& lights, std::string base)
    {
        // trim
        while (!base.empty() && std::isspace(static_cast<unsigned char>(base.back())))
            base.pop_back();
        while (!base.empty() && std::isspace(static_cast<unsigned char>(base.front())))
            base.erase(base.begin());

        if (base.empty())
            base = "Light";

        if (!name_exists(lights, base))
            return base;

        for (int i = 1; i < 10000; ++i)
        {
            char suffix[16] = {};
            std::snprintf(suffix, sizeof(suffix), ".%03d", i);

            std::string candidate = base + suffix;
            if (!name_exists(lights, candidate))
                return candidate;
        }

        return base + ".9999";
    }

} // namespace

CreateLightDialog::CreateLightDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::CreateLightDialog)
{
    ui->setupUi(this);

    setWindowTitle("Create Light");

    if (ui->createButton)
        connect(ui->createButton, &QAbstractButton::clicked, this, &CreateLightDialog::onCreate);

    if (ui->cancelButton)
        connect(ui->cancelButton, &QAbstractButton::clicked, this, &CreateLightDialog::onCancel);

    if (ui->nameEdit)
        connect(ui->nameEdit, &QLineEdit::returnPressed, this, &CreateLightDialog::onCreate);

    // If user types a custom name, clear any warning.
    if (ui->nameEdit)
    {
        connect(ui->nameEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (ui->nameHintLabel)
                ui->nameHintLabel->setText(QString());
        });
    }

    // On type change: if we are still in auto-name mode, regenerate a unique default.
    if (ui->typeCombo && ui->nameEdit)
    {
        connect(ui->typeCombo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                [this](int) {
                    const QString curName = ui->nameEdit->text();
                    if (!should_autoupdate_name(curName))
                        return;

                    if (!m_core)
                    {
                        // No core yet; just set the base default without uniqueness.
                        const QString base = base_name_for_type_label(ui->typeCombo->currentText());
                        ui->nameEdit->setText(base);
                        ui->nameEdit->selectAll();
                        return;
                    }

                    const QString base = base_name_for_type_label(ui->typeCombo->currentText());

                    const std::vector<SceneLight*> lights = m_core->sceneLights();
                    const std::string              unique =
                        make_unique_name(lights, base.toStdString());

                    ui->nameEdit->setText(QString::fromStdString(unique));
                    ui->nameEdit->selectAll();

                    if (ui->nameHintLabel)
                        ui->nameHintLabel->setText(QString());
                });
    }

    // Don’t set the default name here; do it once Core is available (idleEvent),
    // so uniqueness can be computed correctly.
}

CreateLightDialog::~CreateLightDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void CreateLightDialog::idleEvent(Core* core)
{
    m_core = core;

    if (!m_core || !ui->typeCombo || !ui->nameEdit)
        return;

    const QString curName = ui->nameEdit->text();
    if (!should_autoupdate_name(curName))
    {
        return;
    }

    const QString base = base_name_for_type_label(ui->typeCombo->currentText());

    const std::vector<SceneLight*> lights = m_core->sceneLights();
    const std::string              unique = make_unique_name(lights, base.toStdString());

    ui->nameEdit->setText(QString::fromStdString(unique));
    ui->nameEdit->selectAll();

    if (ui->nameHintLabel)
        ui->nameHintLabel->setText(QString());
}

void CreateLightDialog::showEvent(QShowEvent* e)
{
    SubWindowBase::showEvent(e);

    if (!m_core || !ui || !ui->typeCombo || !ui->nameEdit)
        return;

    const QString curName = ui->nameEdit->text();
    if (!should_autoupdate_name(curName))
        return;

    const QString base = base_name_for_type_label(ui->typeCombo->currentText());

    const std::vector<SceneLight*> lights = m_core->sceneLights();
    const std::string              unique = make_unique_name(lights, base.toStdString());

    ui->nameEdit->setText(QString::fromStdString(unique));
    ui->nameEdit->selectAll();

    if (ui->nameHintLabel)
        ui->nameHintLabel->setText(QString());
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
        qname = base_name_for_type_label(typeLabel);

    // Enforce uniqueness at the dialog level (per your request).
    const std::vector<SceneLight*> lights = m_core->sceneLights();
    const std::string              unique = make_unique_name(lights, qname.toStdString());

    if (ui->nameHintLabel && QString::compare(QString::fromStdString(unique), qname, Qt::CaseInsensitive) != 0)
        ui->nameHintLabel->setText("Name already exists. Using a unique name.");

    [[maybe_unused]] SceneLight* created = m_core->createLight(unique, type);

    accept();
}

void CreateLightDialog::onCancel()
{
    reject();
}
