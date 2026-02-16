//============================================================
// TextureEditorDialog.cpp  (FULL REPLACEMENT)
//============================================================
#include "TextureEditorDialog.hpp"

#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QVariant>

#include "Core.hpp"
#include "ImageHandler.hpp"
#include "SubWindows/ui_TextureEditorDialog.h"

namespace
{
    static QString formatFromChannels(int ch)
    {
        switch (ch)
        {
            case 1:
                return "R8";
            case 2:
                return "RG8";
            case 3:
                return "RGB8";
            case 4:
                return "RGBA8";
            default:
                break;
        }
        return "-";
    }

    static QImage::Format qimageFormatFromChannels(int ch)
    {
        switch (ch)
        {
            case 1:
                return QImage::Format_Grayscale8;
            case 3:
                return QImage::Format_RGB888;
            case 4:
                return QImage::Format_RGBA8888; // change to BGRA8888 if your loader outputs BGRA
            default:
                break;
        }
        return QImage::Format_Invalid;
    }
} // namespace

TextureEditorDialog::TextureEditorDialog(QWidget* parent) : SubWindowBase(parent)
{
    ui = new Ui::TextureEditorDialog();
    ui->setupUi(this);

    connect(ui->addButton, &QPushButton::clicked, this, &TextureEditorDialog::onAddTexture);
    connect(ui->removeButton, &QPushButton::clicked, this, &TextureEditorDialog::onRemoveTexture);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(ui->textureListWidget, &QListWidget::currentRowChanged, this, &TextureEditorDialog::onSelectionChanged);
    connect(ui->nameLineEdit, &QLineEdit::textEdited, this, &TextureEditorDialog::onNameEdited);

    // TextureEditor is an IMAGE LIST. Color space is not handled here.
    if (ui->colorSpaceCombo)
        ui->colorSpaceCombo->setVisible(false);
    // if (ui->colorSpaceLabel)
    // ui->colorSpaceLabel->setVisible(false);

    // Make preview area visibly usable
    ui->previewWidgetPlaceholder->setAlignment(Qt::AlignCenter);
    ui->previewWidgetPlaceholder->setMinimumHeight(180);
    ui->previewWidgetPlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->previewWidgetPlaceholder->setStyleSheet("border: 1px solid #3a3a3a;");

    ui->previewWidgetPlaceholder->setText("Preview");
    ui->textureListWidget->setCurrentRow(-1);
}

TextureEditorDialog::~TextureEditorDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void TextureEditorDialog::resizeEvent(QResizeEvent* e)
{
    SubWindowBase::resizeEvent(e);

    // Re-render preview at the new size
    updatePreview(m_core);
}

void TextureEditorDialog::idleEvent(Core* core)
{
    if (!core)
        return;

    if (!m_core)
        m_core = core;

    static uint64_t s_lastStamp = 0;
    const uint64_t  stamp       = core->sceneContentChangeStamp();

    if (stamp != s_lastStamp)
    {
        s_lastStamp = stamp;
        rebuildTextureList(core);
        refreshTextureDetails(core);
    }
}

void TextureEditorDialog::onAddTexture()
{
    if (!m_core)
        return;

    ImageHandler* ih = m_core->imageHandler();
    if (!ih)
        return;

    const QString file = QFileDialog::getOpenFileName(
        this,
        tr("Load Image"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.tga *.bmp *.hdr *.exr *.ktx *.ktx2);;All Files (*.*)"));

    if (file.isEmpty())
        return;

    (void)ih->loadFromFile(file.toStdString(), true);

    rebuildTextureList(m_core);
    refreshTextureDetails(m_core);
}

void TextureEditorDialog::onRemoveTexture()
{
    // ImageHandler currently has no remove(ImageId).
    // Keeping this disabled prevents UI/Core desync.
    // Implement ImageHandler::remove(ImageId) first, then wire this.
}

void TextureEditorDialog::onSelectionChanged()
{
    refreshTextureDetails(m_core);
}

void TextureEditorDialog::onNameEdited(const QString& text)
{
    // Core currently has no "rename image" API.
    // For now, reflect edit in UI only.
    const int row = ui->textureListWidget->currentRow();
    if (row < 0)
        return;

    QListWidgetItem* item = ui->textureListWidget->item(row);
    if (item)
        item->setText(text);
}

void TextureEditorDialog::rebuildTextureList(Core* core)
{
    if (!core)
        return;

    ImageHandler* ih = core->imageHandler();
    if (!ih)
        return;

    const int32_t prevId = currentTextureId();

    ui->textureListWidget->blockSignals(true);
    ui->textureListWidget->clear();
    m_entries.clear();

    const auto& imgs = ih->images();
    m_entries.reserve(imgs.size());

    for (int32_t i = 0; i < static_cast<int32_t>(imgs.size()); ++i)
    {
        const Image& img = imgs[static_cast<size_t>(i)];

        const std::string           imgName   = img.name();
        const std::filesystem::path imgPathFs = img.path();

        QString displayName;

        if (!imgName.empty())
        {
            displayName = QString::fromStdString(imgName);
        }
        else if (!imgPathFs.empty())
        {
            displayName = QString::fromStdString(imgPathFs.stem().string());
        }
        else
        {
            displayName = QString("Image %1").arg(i);
        }

        TextureEntry e = {};
        e.id           = i; // NOTE: currently treating ImageId as index
        e.name         = displayName;
        e.path         = imgPathFs.empty() ? QString() : QString::fromStdString(imgPathFs.string());
        m_entries.push_back(e);

        auto* item = new QListWidgetItem(e.name);
        item->setData(Qt::UserRole, QVariant(e.id));
        ui->textureListWidget->addItem(item);
    }

    ui->textureListWidget->blockSignals(false);

    if (prevId != -1)
    {
        for (int row = 0; row < ui->textureListWidget->count(); ++row)
        {
            QListWidgetItem* it = ui->textureListWidget->item(row);
            if (it && it->data(Qt::UserRole).toInt() == prevId)
            {
                ui->textureListWidget->setCurrentRow(row);
                return;
            }
        }
    }

    ui->textureListWidget->setCurrentRow(ui->textureListWidget->count() > 0 ? 0 : -1);
}

void TextureEditorDialog::refreshTextureDetails(Core* core)
{
    if (!core)
        return;

    ImageHandler* ih = core->imageHandler();
    if (!ih)
        return;

    const int32_t id  = currentTextureId();
    m_selectedImageId = id;

    if (id < 0)
    {
        ui->nameLineEdit->setText(QString());
        ui->pathLineEdit->setText(QString());
        ui->sizeValueLabel->setText("-");
        ui->formatValueLabel->setText("-");
        ui->usedByValueLabel->setText("-");
        ui->previewWidgetPlaceholder->setText("Preview");
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        return;
    }

    const Image* img = ih->get(id);
    if (!img)
        return;

    const QString name = QString::fromStdString(img->name());
    const QString path = QString::fromStdString(img->path().string());

    ui->nameLineEdit->setText(name);
    ui->pathLineEdit->setText(path);

    if (img->width() > 0 && img->height() > 0)
        ui->sizeValueLabel->setText(QString("%1 x %2").arg(img->width()).arg(img->height()));
    else
        ui->sizeValueLabel->setText("-");

    // For KTX/KTX2, channels may be 0 depending on your loader; keep it simple.
    ui->formatValueLabel->setText(formatFromChannels(img->channels()));

    // Requires a "materials reference image X" query; placeholder for now.
    ui->usedByValueLabel->setText("-");

    updatePreview(core);
}

void TextureEditorDialog::updatePreview(Core* core)
{
    if (!core)
        return;

    ImageHandler* ih = core->imageHandler();
    if (!ih)
        return;

    const int32_t id = m_selectedImageId;
    if (id < 0)
    {
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        ui->previewWidgetPlaceholder->setText("Preview");
        return;
    }

    const Image* img = ih->get(id);
    if (!img)
        return;

    // If your Image stores only encoded data (KTX/KTX2) and not decoded pixels,
    // img->data() may be null. In that case show a clear fallback.
    if (!img->data() || img->width() <= 0 || img->height() <= 0)
    {
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        ui->previewWidgetPlaceholder->setText(QString::fromStdString(img->name()) + "\n(No CPU preview)");
        return;
    }

    const QImage::Format fmt = qimageFormatFromChannels(img->channels());
    if (fmt == QImage::Format_Invalid)
    {
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        ui->previewWidgetPlaceholder->setText(QString::fromStdString(img->name()) + "\n(Unsupported format)");
        return;
    }

    const int w   = img->width();
    const int h   = img->height();
    const int bpl = w * img->channels(); // if you have a stride in Image, use it instead

    const QImage wrapped(reinterpret_cast<const uchar*>(img->data()), w, h, bpl, fmt);
    if (wrapped.isNull())
    {
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        ui->previewWidgetPlaceholder->setText(QString::fromStdString(img->name()) + "\n(Preview wrap failed)");
        return;
    }

    // Detach from engine memory so preview remains valid if image storage changes.
    const QImage qimg = wrapped.copy();

    const QSize target = ui->previewWidgetPlaceholder->size();
    if (target.width() <= 1 || target.height() <= 1)
    {
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        ui->previewWidgetPlaceholder->setText(QString::fromStdString(img->name()) + "\n(Preview area too small)");
        return;
    }

    const QPixmap pix = QPixmap::fromImage(
        qimg.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (pix.isNull())
    {
        ui->previewWidgetPlaceholder->setPixmap(QPixmap());
        ui->previewWidgetPlaceholder->setText(QString::fromStdString(img->name()) + "\n(Pixmap failed)");
        return;
    }

    ui->previewWidgetPlaceholder->setText(QString());
    ui->previewWidgetPlaceholder->setPixmap(pix);
}

int32_t TextureEditorDialog::currentTextureId() const noexcept
{
    const int row = ui->textureListWidget->currentRow();
    if (row < 0)
        return -1;

    QListWidgetItem* item = ui->textureListWidget->item(row);
    if (!item)
        return -1;

    return item->data(Qt::UserRole).toInt();
}

int32_t TextureEditorDialog::findTextureByName(const QString& name) const noexcept
{
    for (const TextureEntry& e : m_entries)
    {
        if (e.name == name)
            return e.id;
    }
    return -1;
}
