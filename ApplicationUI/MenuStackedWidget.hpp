#ifndef MENUSTACKEDWIDGET_HPP
#define MENUSTACKEDWIDGET_HPP

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

enum class ButtonType
{
    None,
    Tool,
    Command,
    Action,
    State
};

class MenuStackedWidget : public QStackedWidget
{
    Q_OBJECT

public:
    MenuStackedWidget(QWidget *parent = nullptr);

    QWidget* addNewPage();
    void addLabel(int pageIndex, const QString &text);
    void addButton(int pageIndex, const QString &text, ButtonType type = ButtonType::None, const QString &id = "", QKeySequence shortcut = {});

    void addIncrementControl(int pageIndex, const QString& labelText, const QString& id, const QKeySequence& decKey = QKeySequence(), const QKeySequence& incKey = QKeySequence());

    void adjustPageSize();

    // Called to drop all tools (usually from selection change)
    void externalToolClicked();

    void setToolChecked(const QString& id, bool checked);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void sideMenuButtonClicked(ButtonType type, const QString &id, bool checked, int delta = 0);

private slots:
    void onCurrentChanged(int index);
    void onButtonClicked(QPushButton* button, ButtonType type, const QString &id, bool checked);
    void onIncrementControlButtonClicked();

private:
    QList<QPushButton*> m_buttons;
};

#endif // MENUSTACKEDWIDGET_HPP
