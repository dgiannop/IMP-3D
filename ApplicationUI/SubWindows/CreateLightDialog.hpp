#ifndef CREATELIGHTDIALOG_HPP
#define CREATELIGHTDIALOG_HPP

#include <QString>

#include "SubWindowBase.hpp"

namespace Ui
{
    class CreateLightDialog;
} // namespace Ui

/**
 * @brief Dialog for creating a new scene light.
 *
 * Provides a minimal UI to specify a light name and type,
 * then delegates creation to Core.
 */
class CreateLightDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit CreateLightDialog(QWidget* parent = nullptr);
    ~CreateLightDialog() noexcept override;

    /** @copydoc SubWindowBase::idleEvent */
    void idleEvent(class Core* core) override;

protected:
    void showEvent(QShowEvent* e) override;

private slots:
    void onCreate();
    void onCancel();

private:
    Ui::CreateLightDialog* ui     = nullptr;
    Core*                  m_core = nullptr;
};

#endif // CREATELIGHTDIALOG_HPP
