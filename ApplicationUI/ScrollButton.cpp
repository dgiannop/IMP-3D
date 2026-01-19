#include "ScrollButton.hpp"

#include <QCursor>
#include <QMouseEvent>

ScrollButton::ScrollButton(QWidget *parent)
    : QPushButton(parent), buttonDragging(false)
{
    installEventFilter(this);
}

bool ScrollButton::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                buttonDragging = true;
                dragStartPos = QCursor::pos();
                setCursor(Qt::BlankCursor);
            }
        }
        else if (event->type() == QEvent::MouseMove && buttonDragging)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint delta = QCursor::pos() - dragStartPos;
            QCursor::setPos(dragStartPos);
            emit scrollButtonAction(this, delta);
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                buttonDragging = false;
                setCursor(Qt::ArrowCursor);
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
