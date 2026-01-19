#ifndef SCROLLBUTTON_HPP
#define SCROLLBUTTON_HPP

#include <QPushButton>
#include <QWidget>

class ScrollButton : public QPushButton
{
    Q_OBJECT

public:
    ScrollButton(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void scrollButtonAction(QWidget* sender, QPoint delta);

private:
    bool   buttonDragging;
    QPoint dragStartPos;
};

#endif // SCROLLBUTTON_HPP
