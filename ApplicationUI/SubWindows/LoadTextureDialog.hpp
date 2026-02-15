#ifndef LOADTEXTUREDIALOG_HPP
#define LOADTEXTUREDIALOG_HPP

#include <QString>

#include "SubWindowBase.hpp"

namespace Ui
{
    class LoadTextureDialog;
} // namespace Ui

class LoadTextureDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit LoadTextureDialog(QWidget* parent = nullptr);
    ~LoadTextureDialog() noexcept override;

    void idleEvent(class Core* core) override;

    QString filePath() const;
    QString displayName() const;
    bool    isSrgb() const;

private slots:
    void onBrowse();
    void onLoad();
    void onCancel();
    void onFileEdited(const QString& text);

private:
    Ui::LoadTextureDialog* ui = nullptr;

    Core* m_core = nullptr;
};

#endif // LOADTEXTUREDIALOG_HPP
