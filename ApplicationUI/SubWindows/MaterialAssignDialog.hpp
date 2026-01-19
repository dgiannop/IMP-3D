#ifndef MATERIALASSIGNDIALOG_HPP
#define MATERIALASSIGNDIALOG_HPP

#include <QColor>
#include <QString>
#include <vector>

#include "SubWindowBase.hpp"

class QCompleter;
class QStringListModel;

namespace Ui
{
    class MaterialAssignDialog;
} // namespace Ui

class MaterialAssignDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit MaterialAssignDialog(QWidget* parent = nullptr);
    ~MaterialAssignDialog() noexcept override;

    void idleEvent(class Core* core) override;

private slots:
    void onPickColor();
    void onAssign();
    void onNameEdited(const QString& text);
    void onComboChanged(int index);

private:
    void    rebuildMaterialList(Core* core);
    void    applySwatchColor(const QColor& c);
    int32_t findMaterialByName(const QString& name) const noexcept;

private:
    Ui::MaterialAssignDialog* ui = nullptr;

    QCompleter*       m_completer = nullptr;
    QStringListModel* m_model     = nullptr;

    QColor m_baseColor = QColor(128, 128, 128);

    struct MaterialEntry
    {
        int32_t id = 0;
        QString name;
        QColor  baseColor;
    };

    std::vector<MaterialEntry> m_entries;
    Core*                      m_core = nullptr;

    SysCounterPtr m_lastMatCounter = {};
    uint64_t      m_lastMatStamp   = 0; // or whatever your SysCounter exposes
    bool          m_hasInitialList = false;
};

#endif // MATERIALASSIGNDIALOG_HPP
