#include "ShortcutButton.hpp"
#include <QPainter>
#include <QStyleOptionButton>

ShortcutButton::ShortcutButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setText(text); // Only base label, no shortcut
}

ShortcutButton::ShortcutButton(QWidget* parent)
    : QPushButton(parent)
{
}

void ShortcutButton::setShortcutText(const QString& text)
{
    m_shortcutText = text;
    update();
}

void ShortcutButton::setPadding(int left, int right)
{
    m_paddingLeft = left;
    m_paddingRight = right;
    update();
}

void ShortcutButton::paintEvent(QPaintEvent* event)
{
    QStyleOptionButton option;
    initStyleOption(&option);

    QPainter painter(this);
    style()->drawControl(QStyle::CE_PushButtonBevel, &option, &painter, this);

           // Draw main label (left-aligned)
    QRect textRect = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
    painter.setFont(font());
    painter.setPen(option.palette.buttonText().color());

    QString mainText = text();
    // QRect leftRect = textRect.adjusted(m_paddingLeft, 0, 0, 0);
    // painter.drawText(leftRect, Qt::AlignVCenter | Qt::AlignLeft, mainText);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, mainText);

    // Draw shortcut (right-aligned)
    if (!m_shortcutText.isEmpty())
    {
        QRect rightRect = textRect.adjusted(0, 0, -m_paddingRight, 0);

        // Make color slightly dimmer
        QColor shortcutColor = option.palette.buttonText().color();
        shortcutColor.setAlphaF(0.4);
        painter.setPen(shortcutColor);

        // Shrink font size slightly
        QFont smallFont = font();
#if 0 // Use a cross-plaform monospace font
        smallFont.setFamily("Courier New"); // Cross-platform
#else // Let QT deside the best monospace font for the system
        smallFont.setStyleHint(QFont::Monospace);
        smallFont.setFixedPitch(true);
#endif
        smallFont.setPointSizeF(smallFont.pointSizeF() * 0.8);
        painter.setFont(smallFont);

        // Draw right-aligned shortcut
        painter.drawText(rightRect, Qt::AlignVCenter | Qt::AlignRight, m_shortcutText);
    }
}
