//==============================================================
// LightsEditorDialog.cpp
//==============================================================
#include "LightsEditorDialog.hpp"

#include <QAbstractButton>
#include <QSplitter>

#include "SubWindows/ui_LightsEditorDialog.h"

LightsEditorDialog::LightsEditorDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::LightsEditorDialog)
{
    ui->setupUi(this);

    setWindowTitle("Lights Editor");

    setMinimumSize(520, 180);
    setMaximumSize(900, 800);

    m_expandedMinSize = minimumSize();
    m_expandedMaxSize = maximumSize();

    ui->rightPanel->setMinimumWidth(300);
    ui->rightPanel->setMaximumWidth(300);

    m_rightPanelMinW = ui->rightPanel->minimumWidth();
    m_rightPanelMaxW = ui->rightPanel->maximumWidth();

    m_leftIndex  = 0;
    m_rightIndex = 1;

    if (ui->splitterMain)
    {
        if (ui->splitterMain->count() >= 2)
        {
            for (int i = 0; i < ui->splitterMain->count(); ++i)
            {
                QWidget* w = ui->splitterMain->widget(i);
                if (!w)
                    continue;

                if (w->objectName() == "leftPanel")
                    m_leftIndex = i;
                else if (w->objectName() == "rightPanel")
                    m_rightIndex = i;
            }
        }

        ui->splitterMain->setStretchFactor(m_leftIndex, 1);
        ui->splitterMain->setStretchFactor(m_rightIndex, 0);
        ui->splitterMain->setCollapsible(m_leftIndex, true);
        ui->splitterMain->setCollapsible(m_rightIndex, false);
    }

    // Optional: if your .ui has a Close button like the screenshot windows
    if (ui->closeButton)
        connect(ui->closeButton, &QAbstractButton::clicked, this, &QWidget::close);

    // Toggle left collapse
    if (ui->toggleLeftButton)
        connect(ui->toggleLeftButton, &QAbstractButton::clicked, this, &LightsEditorDialog::onToggleLeft);

    // Start expanded
    m_leftCollapsed    = false;
    m_lastExpandedSize = sizeHint();
}

LightsEditorDialog::~LightsEditorDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void LightsEditorDialog::idleEvent(Core* /*core*/)
{
    // Intentionally empty for now.
    // Later: refresh light list based on scene counter, etc.
}

void LightsEditorDialog::onToggleLeft()
{
    applyCollapsedState(!m_leftCollapsed);
}

void LightsEditorDialog::applyCollapsedState(bool collapsed, bool force)
{
    if (!ui || !ui->splitterMain || !ui->rightPanel || !ui->leftPanel)
        return;

    if (!force && collapsed == m_leftCollapsed)
        return;

    if (!m_leftCollapsed && collapsed)
        m_lastExpandedSize = size();

    m_leftCollapsed = collapsed;

    if (collapsed)
    {
        ui->leftPanel->setVisible(false);

        // Let right panel actually fill the dialog in collapsed mode
        ui->rightPanel->setMinimumWidth(0);
        ui->rightPanel->setMaximumWidth(QWIDGETSIZE_MAX);
        ui->rightPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QList<int> sizes;
        sizes.reserve(ui->splitterMain->count());
        for (int i = 0; i < ui->splitterMain->count(); ++i)
            sizes.push_back((i == m_rightIndex) ? 1 : 0);
        ui->splitterMain->setSizes(sizes);

        // Compute tight width from sizeHint + frame margins (prevents “gap”)
        const int marginW =
            (layout() ? (layout()->contentsMargins().left() + layout()->contentsMargins().right()) : 0) + (ui->splitterMain ? (ui->splitterMain->contentsMargins().left() + ui->splitterMain->contentsMargins().right()) : 0);

        const int newW = ui->rightPanel->sizeHint().width() + marginW + 2; // +2 safety
        setFixedWidth(std::max(newW, minimumSizeHint().width()));
        resize(width(), height());
    }
    else
    {
        ui->leftPanel->setVisible(true);

        // Restore right panel constraints for expanded mode
        ui->rightPanel->setMinimumWidth(m_rightPanelMinW);
        ui->rightPanel->setMaximumWidth(m_rightPanelMaxW);
        ui->rightPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        setMinimumSize(m_expandedMinSize);
        setMaximumSize(m_expandedMaxSize);

        const int rightW = ui->rightPanel->maximumWidth();

        if (m_lastExpandedSize.width() < rightW + 100)
            m_lastExpandedSize.setWidth(rightW + 180);
        if (m_lastExpandedSize.height() < minimumHeight())
            m_lastExpandedSize.setHeight(minimumHeight());

        resize(m_lastExpandedSize);

        const int totalW = width();
        const int leftW  = std::max(0, totalW - rightW);

        QList<int> sizes;
        sizes.reserve(ui->splitterMain->count());
        for (int i = 0; i < ui->splitterMain->count(); ++i)
        {
            if (i == m_leftIndex)
                sizes.push_back(leftW);
            else if (i == m_rightIndex)
                sizes.push_back(rightW);
            else
                sizes.push_back(0);
        }
        ui->splitterMain->setSizes(sizes);
    }
}
