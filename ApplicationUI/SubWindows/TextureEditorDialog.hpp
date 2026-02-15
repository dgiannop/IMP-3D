//============================================================
// TextureEditorDialog.hpp  (FULL REPLACEMENT)
//============================================================
#ifndef TEXTUREEDITORDIALOG_HPP
#define TEXTUREEDITORDIALOG_HPP

#include <QString>
#include <vector>

#include "SubWindowBase.hpp"

class Core;

namespace Ui
{
    class TextureEditorDialog;
} // namespace Ui

class TextureEditorDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit TextureEditorDialog(QWidget* parent = nullptr);
    ~TextureEditorDialog() noexcept override;

    void idleEvent(Core* core) override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void onAddTexture();
    void onRemoveTexture();
    void onSelectionChanged();
    void onNameEdited(const QString& text);

private:
    void    rebuildTextureList(Core* core);
    void    refreshTextureDetails(Core* core);
    void    updatePreview(Core* core);
    int32_t currentTextureId() const noexcept;
    int32_t findTextureByName(const QString& name) const noexcept;

private:
    Ui::TextureEditorDialog* ui = nullptr;

    struct TextureEntry
    {
        int32_t id = 0;
        QString name;
        QString path;
    };

    std::vector<TextureEntry> m_entries;
    Core*                     m_core            = nullptr;
    int32_t                   m_selectedImageId = -1;
};

#endif // TEXTUREEDITORDIALOG_HPP
