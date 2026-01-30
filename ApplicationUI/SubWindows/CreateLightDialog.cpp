#include "CreateLightDialog.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

#include "Core.hpp"
#include "SubWindows/ui_CreateLightDialog.h"

CreateLightDialog::CreateLightDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::CreateLightDialog)
{
    ui->setupUi(this);

    setWindowTitle("Create Light");
    resize(420, 160);
    setMinimumSize(360, 140);
    setMaximumSize(600, 200);

    if (ui->titleLabel)
        ui->titleLabel->setText("Create Light");

    // Default name based on initial type
    ui->nameEdit->setText("Directional Light");

    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->createButton, &QPushButton::clicked, this, &CreateLightDialog::onCreate);
    connect(ui->typeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &CreateLightDialog::onTypeChanged);
}

CreateLightDialog::~CreateLightDialog() noexcept
{
    delete ui;
}

void CreateLightDialog::idleEvent(Core* core)
{
    // Core pointer is cached for use when Create is pressed.
    m_core = core;
}

void CreateLightDialog::onTypeChanged(int index)
{
    // Simple auto-naming behavior; can be refined later.
    switch (index)
    {
        case 0:
            ui->nameEdit->setText("Directional Light");
            break;
        case 1:
            ui->nameEdit->setText("Point Light");
            break;
        case 2:
            ui->nameEdit->setText("Spot Light");
            break;
        default:
            break;
    }
}

void CreateLightDialog::onCreate()
{
    if (!m_core)
    {
        reject();
        return;
    }

    const QString qname = ui->nameEdit->text().trimmed();
    if (qname.isEmpty())
        return;

    const int typeIndex = ui->typeCombo->currentIndex();

    // TODO:
    // Map typeIndex -> LightType enum
    // m_core->createSceneLight(qname.toStdString(), type);

    accept();
}
