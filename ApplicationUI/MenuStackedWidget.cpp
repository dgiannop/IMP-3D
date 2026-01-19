#include "MenuStackedWidget.hpp"

#include <QApplication>
#include <QResizeEvent>
#include <QShortcut>

#include "ShortcutButton.hpp"

MenuStackedWidget::MenuStackedWidget(QWidget *parent) : QStackedWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(this, &QStackedWidget::currentChanged, this, &MenuStackedWidget::onCurrentChanged);
}

QWidget* MenuStackedWidget::addNewPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    page->setLayout(layout);
    addWidget(page);
    return page;
}

void MenuStackedWidget::addLabel(int pageIndex, const QString &text)
{
    QWidget* page = widget(pageIndex);
    if (!page)
        return; // Ensure the page exists

    QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(page->layout());
    if (layout)
    {
        QLabel *label = new QLabel(text, page);
        label->setFixedHeight(24);
        label->setMinimumWidth(120);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->addWidget(label);
        adjustPageSize();
    }
}

void MenuStackedWidget::addButton(int pageIndex, const QString& text, ButtonType type, const QString& id, QKeySequence shortcut)
{
    QWidget* page = widget(pageIndex);
    if (!page)
        return; // Ensure the page exists

    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(page->layout());
    if (!layout)
        return;

    // Use ShortcutButton instead of QPushButton
    ShortcutButton* button = new ShortcutButton(text, page);
    button->setFixedHeight(24);
    button->setMinimumWidth(120);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setProperty("type", static_cast<int>(type));
    button->setProperty("id", id);

    if (!shortcut.isEmpty())
    {
        QString shortcutText = shortcut.toString(QKeySequence::NativeText);
        button->setShortcutText(shortcutText); // ✅ This displays it in the button

        QShortcut* sc = new QShortcut(shortcut, this);
        connect(sc, &QShortcut::activated, button, &QPushButton::click);
    }

    if (type == ButtonType::Tool)
    {
        button->setCheckable(true);
    }

    layout->addWidget(button);

    if (type != ButtonType::Tool)
    {
        connect(button, &QPushButton::clicked, this, [this, button, type, id](bool checked) {
            onButtonClicked(button, type, id, checked);
        });
    }
    else
    {
        connect(button, &QPushButton::toggled, this, [this, button, type, id](bool checked) {
            onButtonClicked(button, type, id, checked);
        });
    }

    m_buttons.push_back(button);

    adjustPageSize();
}

void MenuStackedWidget::addIncrementControl(int                 pageIndex,
                                            const QString&      labelText,
                                            const QString&      id,
                                            const QKeySequence& decKey,
                                            const QKeySequence& incKey)
{
    QWidget* page = widget(pageIndex);
    if (!page)
        return;

    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(page->layout());
    if (!mainLayout)
        return;

    QWidget*     controlWidget = new QWidget(page);
    QHBoxLayout* hLayout       = new QHBoxLayout(controlWidget);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(1);

    QLabel* label = new QLabel(labelText, controlWidget);
    label->setFixedHeight(24);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QPushButton* btnPlus = new QPushButton("+", controlWidget);
    btnPlus->setFixedSize(24, 24);
    btnPlus->setStyleSheet("padding-left: 8px; font-weight: bold; font-size: 10pt; font-family: monospace;");

    QPushButton* btnMinus = new QPushButton("−", controlWidget);
    btnMinus->setFixedSize(24, 24);
    btnMinus->setStyleSheet("padding-left: 9px; font-weight: bold; font-size: 10pt; font-family: monospace;");

    btnPlus->setProperty("id", id);
    btnPlus->setProperty("delta", +1);
    btnMinus->setProperty("id", id);
    btnMinus->setProperty("delta", -1);

    hLayout->addWidget(label);
    hLayout->addWidget(btnPlus);
    hLayout->addWidget(btnMinus);

    mainLayout->addWidget(controlWidget);
    adjustPageSize();

    connect(btnPlus, &QPushButton::clicked, this, &MenuStackedWidget::onIncrementControlButtonClicked);
    connect(btnMinus, &QPushButton::clicked, this, &MenuStackedWidget::onIncrementControlButtonClicked);

    // Shortcuts if provided
    if (!decKey.isEmpty())
    {
        auto* decSc = new QShortcut(decKey, this);
        decSc->setContext(Qt::WindowShortcut);
        connect(decSc, &QShortcut::activated, this, [this, id]() {
            emit sideMenuButtonClicked(ButtonType::Action, id, false, -1);
        });
    }
    if (!incKey.isEmpty())
    {
        auto* incSc = new QShortcut(incKey, this);
        incSc->setContext(Qt::WindowShortcut);
        connect(incSc, &QShortcut::activated, this, [this, id]() {
            emit sideMenuButtonClicked(ButtonType::Action, id, false, +1);
        });
    }
}

void MenuStackedWidget::resizeEvent(QResizeEvent *event)
{
    adjustPageSize();
    QStackedWidget::resizeEvent(event);
}

void MenuStackedWidget::adjustPageSize()
{
    if (currentWidget())
    {
        QSize sizeHint = currentWidget()->sizeHint();
        int newHeight = sizeHint.height();
        int newWidth = sizeHint.width();

        setFixedHeight(newHeight);
        setFixedWidth(newWidth);
    }
}

void MenuStackedWidget::onCurrentChanged(int index)
{
    Q_UNUSED(index);
    adjustPageSize();
}

void MenuStackedWidget::onButtonClicked(QPushButton* button, ButtonType type, const QString &id, bool checked)
{
    if (id.isEmpty())
    {
        qWarning() << "[MenuStackedWidget] Ignoring button click with empty ID";
        return;  // I could probably log or show this somewhere.
    }

    for (QPushButton* btn : std::as_const(m_buttons))
    {
        ButtonType btnType = static_cast<ButtonType>(btn->property("type").toInt());

        if (btnType == ButtonType::Tool)
        {
            if (btn && btn != button && btn->isChecked())
            {
                btn->blockSignals(true);
                btn->setChecked(false);
                btn->blockSignals(false);
                // emit buttonUnpressed(btn->text());
            }
        }
    }

    emit sideMenuButtonClicked(type, id, button->isChecked());
}

void MenuStackedWidget::onIncrementControlButtonClicked()
{
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button)
        return;

    QString id = button->property("id").toString();
    int delta = button->property("delta").toInt();

    emit sideMenuButtonClicked(ButtonType::Action, id, button->isChecked(), delta);
}

void MenuStackedWidget::externalToolClicked()
{
    for (int i = 0; i < m_buttons.size(); ++i)
    {
        QPushButton* btn = m_buttons[i];
        ButtonType type = static_cast<ButtonType>(btn->property("type").toInt());

        if (type == ButtonType::Tool)
        {
            if (btn && btn->isChecked())
            {
                btn->blockSignals(true);
                btn->setChecked(false);
                btn->blockSignals(false);
                // emit buttonUnpressed(btn->text()); // Emit signal for unpressed button
            }
        }
    }
}

void MenuStackedWidget::setToolChecked(const QString& id, bool checked)
{
    if (id.isEmpty())
        return;

    for (QPushButton* btn : std::as_const(m_buttons))
    {
        if (!btn)
            continue;

        const ButtonType type = static_cast<ButtonType>(btn->property("type").toInt());
        if (type != ButtonType::Tool)
            continue;

        const QString bid = btn->property("id").toString();
        if (bid == id)
        {
            if (btn->isChecked() == checked)
                return;

            btn->blockSignals(true);
            btn->setChecked(checked);
            btn->blockSignals(false);
            return;
        }
    }
}
