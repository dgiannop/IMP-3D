//============================================================
// LoadTextureDialog.cpp  (STUB - OPENS WINDOW)
//============================================================
#include "LoadTextureDialog.hpp"

#include <QFileDialog>
#include <QFileInfo>

#include "SubWindows/ui_LoadTextureDialog.h"

LoadTextureDialog::LoadTextureDialog(QWidget* parent) : SubWindowBase(parent)
{
    ui = new Ui::LoadTextureDialog();
    ui->setupUi(this);

    connect(ui->browseButton, &QPushButton::clicked, this, &LoadTextureDialog::onBrowse);
    connect(ui->loadButton, &QPushButton::clicked, this, &LoadTextureDialog::onLoad);
    connect(ui->cancelButton, &QPushButton::clicked, this, &LoadTextureDialog::onCancel);
    connect(ui->fileLineEdit, &QLineEdit::textEdited, this, &LoadTextureDialog::onFileEdited);

    ui->colorSpaceCombo->setCurrentIndex(0);
}

LoadTextureDialog::~LoadTextureDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void LoadTextureDialog::idleEvent(Core* core)
{
    // STUB: keep pointer for later use (loading via core)
    if (!m_core)
    {
        m_core = core;
    }
}

QString LoadTextureDialog::filePath() const
{
    return ui->fileLineEdit->text();
}

QString LoadTextureDialog::displayName() const
{
    return ui->nameLineEdit->text();
}

bool LoadTextureDialog::isSrgb() const
{
    return (ui->colorSpaceCombo->currentIndex() == 0);
}

void LoadTextureDialog::onBrowse()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Load Texture",
        QString(),
        "Images (*.png *.jpg *.jpeg *.tga *.bmp *.hdr *.exr);;All Files (*.*)");

    if (file.isEmpty())
    {
        return;
    }

    ui->fileLineEdit->setText(file);

    if (ui->nameLineEdit->text().isEmpty())
    {
        const QFileInfo fi(file);
        ui->nameLineEdit->setText(fi.completeBaseName());
    }
}

void LoadTextureDialog::onLoad()
{
    // STUB: accept if file path exists (non-empty)
    if (ui->fileLineEdit->text().isEmpty())
    {
        return;
    }

    accept();
}

void LoadTextureDialog::onCancel()
{
    reject();
}

void LoadTextureDialog::onFileEdited(const QString& text)
{
    // STUB: auto-fill name if empty
    if (!ui->nameLineEdit->text().isEmpty())
    {
        return;
    }

    QFileInfo fi(text);
    if (fi.exists())
    {
        ui->nameLineEdit->setText(fi.completeBaseName());
    }
}
