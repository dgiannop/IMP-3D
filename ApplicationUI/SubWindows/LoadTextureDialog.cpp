// LoadTextureDialog.cpp
#include "LoadTextureDialog.hpp"

#include <QFileDialog>
#include <QFileInfo>

#include "Core.hpp"
#include "ImageHandler.hpp"
#include "SubWindows/ui_LoadTextureDialog.h"

LoadTextureDialog::LoadTextureDialog(QWidget* parent) : SubWindowBase(parent)
{
    ui = new Ui::LoadTextureDialog();
    ui->setupUi(this);

    connect(ui->browseButton, &QPushButton::clicked, this, &LoadTextureDialog::onBrowse);
    connect(ui->loadButton, &QPushButton::clicked, this, &LoadTextureDialog::onLoad);
    connect(ui->cancelButton, &QPushButton::clicked, this, &LoadTextureDialog::onCancel);
    connect(ui->fileLineEdit, &QLineEdit::textEdited, this, &LoadTextureDialog::onFileEdited);
}

LoadTextureDialog::~LoadTextureDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void LoadTextureDialog::idleEvent(Core* core)
{
    m_core = core;
}

QString LoadTextureDialog::filePath() const
{
    return ui->fileLineEdit->text();
}

QString LoadTextureDialog::displayName() const
{
    return ui->nameLineEdit->text();
}

void LoadTextureDialog::onBrowse()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Load Texture",
        QString(),
        "Images (*.png *.jpg *.jpeg *.tga *.bmp *.hdr *.exr);;All Files (*.*)");

    if (file.isEmpty())
        return;

    ui->fileLineEdit->setText(file);

    if (ui->nameLineEdit->text().isEmpty())
    {
        const QFileInfo fi(file);
        ui->nameLineEdit->setText(fi.completeBaseName());
    }
}

void LoadTextureDialog::onLoad()
{
    if (!m_core)
        return;

    ImageHandler* ih = m_core->imageHandler();
    if (!ih)
        return;

    const QString qpath = ui->fileLineEdit->text().trimmed();
    if (qpath.isEmpty())
        return;

    const QFileInfo fi(qpath);
    if (!fi.exists() || !fi.isFile())
        return;

    const std::filesystem::path path = fi.absoluteFilePath().toStdString();

    // Load (reuses if already loaded).
    const ImageId id = ih->loadFromFile(path, /*flipY=*/true);
    if (id == kInvalidImageId)
        return;

    m_loadedId = id;

    // Optional: apply user display name if your Image supports renaming.
    const QString qname = ui->nameLineEdit->text().trimmed();
    if (!qname.isEmpty())
    {
        if (Image* img = ih->get(id))
        {
            // If Image has a setter like: void name(const std::string&)
            if constexpr (requires(Image* i) { i->setName(std::string{}); })
            {
                img->setName(qname.toStdString());
            }
        }
    }

    accept();
}

void LoadTextureDialog::onCancel()
{
    reject();
}

void LoadTextureDialog::onFileEdited(const QString& text)
{
    if (!ui->nameLineEdit->text().isEmpty())
        return;

    QFileInfo fi(text);
    if (fi.exists())
        ui->nameLineEdit->setText(fi.completeBaseName());
}
