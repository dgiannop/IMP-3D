#ifndef CREATELIGHTDIALOG_HPP
#define CREATELIGHTDIALOG_HPP

#include "SubWindowBase.hpp"

namespace Ui
{
    class CreateLightDialog;
} // namespace Ui

class CreateLightDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit CreateLightDialog(QWidget* parent = nullptr);
    ~CreateLightDialog() noexcept override;

    void idleEvent(class Core* core) override;

private slots:
    void onCreate();
    void onTypeChanged(int index);

private:
    Ui::CreateLightDialog* ui     = nullptr;
    Core*                  m_core = nullptr;
};

#endif // CREATELIGHTDIALOG_HPP
