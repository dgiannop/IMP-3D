//============================================================
// TextureEditorDialog.cpp
//============================================================
#include "TextureEditorDialog.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>

#include "SubWindows/ui_TextureEditorDialog.h"

namespace
{
    void setLabelColumnWidth(QLabel* label, int w)
    {
        if (!label)
            return;
        label->setMinimumWidth(w);
        label->setMaximumWidth(w);
    }
} // namespace

TextureEditorDialog::TextureEditorDialog(QWidget* parent) : SubWindowBase(parent)
{
    ui = new Ui::TextureEditorDialog();
    ui->setupUi(this);

    // Match Material Editor feel: fixed label column width.
    // This works regardless of whether you used a QFormLayout or QGridLayout.
    constexpr int kLabelW = 120;
    setLabelColumnWidth(ui->nameLabel, kLabelW);
    setLabelColumnWidth(ui->pathLabel, kLabelW);
    setLabelColumnWidth(ui->sizeLabel, kLabelW);
    setLabelColumnWidth(ui->formatLabel, kLabelW);
    setLabelColumnWidth(ui->colorSpaceLabel, kLabelW);
    setLabelColumnWidth(ui->usedByLabel, kLabelW);

    // Basic wiring
    connect(ui->addButton, &QPushButton::clicked, this, &TextureEditorDialog::onAddTexture);
    connect(ui->removeButton, &QPushButton::clicked, this, &TextureEditorDialog::onRemoveTexture);

    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(ui->textureListWidget, &QListWidget::currentRowChanged, this, &TextureEditorDialog::onSelectionChanged);
    connect(ui->nameLineEdit, &QLineEdit::textEdited, this, &TextureEditorDialog::onNameEdited);
    connect(ui->colorSpaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TextureEditorDialog::onColorSpaceChanged);

    // Preview placeholder
    ui->previewWidgetPlaceholder->setText("Preview (stub)");

    // Populate stub list once
    ui->textureListWidget->addItem("DefaultTexture (stub)");
    ui->textureListWidget->setCurrentRow(0);
}

TextureEditorDialog::~TextureEditorDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void TextureEditorDialog::idleEvent(Core* core)
{
    if (!m_core)
    {
        m_core = core;
    }
}

void TextureEditorDialog::onAddTexture()
{
    ui->textureListWidget->addItem("LoadedTexture (stub)");
}

void TextureEditorDialog::onRemoveTexture()
{
    const int row = ui->textureListWidget->currentRow();
    if (row < 0)
    {
        return;
    }

    delete ui->textureListWidget->takeItem(row);
}

void TextureEditorDialog::onSelectionChanged()
{
    const int row = ui->textureListWidget->currentRow();
    if (row < 0)
    {
        ui->nameLineEdit->setText(QString());
        ui->pathLineEdit->setText(QString());
        ui->sizeValueLabel->setText("-");
        ui->formatValueLabel->setText("-");
        ui->usedByValueLabel->setText("-");
        ui->previewWidgetPlaceholder->setText("Preview (stub)");
        return;
    }

    QListWidgetItem* item = ui->textureListWidget->item(row);
    const QString    name = item ? item->text() : QString();

    ui->nameLineEdit->setText(name);
    ui->pathLineEdit->setText("C:/path/to/" + name + ".png (stub)");
    ui->sizeValueLabel->setText("1024 x 1024 (stub)");
    ui->formatValueLabel->setText("RGBA8 (stub)");
    ui->usedByValueLabel->setText("0 (stub)");
    ui->previewWidgetPlaceholder->setText(name + "\nPreview (stub)");
}

void TextureEditorDialog::onNameEdited(const QString& text)
{
    const int row = ui->textureListWidget->currentRow();
    if (row < 0)
    {
        return;
    }

    QListWidgetItem* item = ui->textureListWidget->item(row);
    if (item)
    {
        item->setText(text);
    }
}

void TextureEditorDialog::onColorSpaceChanged(int index)
{
    (void)index;
}

// --- helpers (stubs) ---

void TextureEditorDialog::rebuildTextureList(Core* core)
{
    (void)core;
}

void TextureEditorDialog::refreshTextureDetails(Core* core)
{
    (void)core;
}

int32_t TextureEditorDialog::currentTextureId() const noexcept
{
    return -1;
}

int32_t TextureEditorDialog::findTextureByName(const QString& name) const noexcept
{
    (void)name;
    return -1;
}
