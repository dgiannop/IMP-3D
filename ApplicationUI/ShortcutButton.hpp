#pragma once

#include <QPushButton>
#include <QKeySequence>

class ShortcutButton : public QPushButton
{
    Q_OBJECT

public:
    explicit ShortcutButton(const QString& text, QWidget* parent = nullptr);
    explicit ShortcutButton(QWidget* parent = nullptr); // For .ui. Must set shortcut in code

    void setShortcutText(const QString& text);
    void setPadding(int left, int right);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_shortcutText;
    int m_paddingLeft = 8;
    int m_paddingRight = 8;
};
