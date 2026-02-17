#pragma once

#include <QString>

#include "SubWindowBase.hpp"

class Core;

namespace Ui
{
    class LoadTextureDialog;
} // namespace Ui

class LoadTextureDialog : public SubWindowBase
{
    Q_OBJECT

public:
    explicit LoadTextureDialog(QWidget* parent = nullptr);
    ~LoadTextureDialog() noexcept override;

    void idleEvent(Core* core) override;

    QString filePath() const;
    QString displayName() const;

    // New: what got loaded (valid after accept()).
    ImageId loadedImageId() const noexcept { return m_loadedId; }

private slots:
    void onBrowse();
    void onLoad();
    void onCancel();
    void onFileEdited(const QString& text);

private:
    Ui::LoadTextureDialog* ui     = nullptr;
    Core*                  m_core = nullptr;

    ImageId m_loadedId = kInvalidImageId;
};
