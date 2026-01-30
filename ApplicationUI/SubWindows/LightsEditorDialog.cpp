//==============================================================
// LightsEditorDialog.cpp
//==============================================================
#include "LightsEditorDialog.hpp"

#include <QAbstractButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSplitter>

#include "Core.hpp"
#include "SubWindows/ui_LightsEditorDialog.h"

// If SceneLight is in some header, include it here.
// Adjust include path to your project layout.
#include "SceneLight.hpp"

namespace
{
    static uint64_t counter_stamp(const SysCounterPtr& c) noexcept
    {
        return c ? c->value() : 0ull;
    }

    static QString to_qstring(std::string_view s)
    {
        return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
    }

} // namespace

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

    // Close button
    if (ui->closeButton)
        connect(ui->closeButton, &QAbstractButton::clicked, this, &QWidget::close);

    // Toggle left collapse
    if (ui->toggleLeftButton)
        connect(ui->toggleLeftButton, &QAbstractButton::clicked, this, &LightsEditorDialog::onToggleLeft);

    // Start expanded
    m_leftCollapsed    = false;
    m_lastExpandedSize = sizeHint();

    // Light list defaults
    if (ui->lightList)
        ui->lightList->setSelectionMode(QAbstractItemView::SingleSelection);
}

LightsEditorDialog::~LightsEditorDialog() noexcept
{
    delete ui;
    ui = nullptr;
}

void LightsEditorDialog::idleEvent(Core* core)
{
    if (!core || !ui || !ui->lightList)
        return;

    const uint64_t stamp = core->sceneChangeStamp();
    if (stamp == m_lastSceneStamp)
        return;

    m_lastSceneStamp = stamp;

    ui->lightList->clear();

    const auto lights = core->sceneLights();
    for (const SceneLight* light : lights)
    {
        if (!light)
            continue;

        ui->lightList->addItem(QString::fromUtf8(light->name().data(),
                                                 static_cast<int>(light->name().size())));
    }
}

void LightsEditorDialog::rebuildLightList(Core* core)
{
    if (!ui || !ui->lightList || !core)
        return;

    const QString prevSel = currentSelectedName();

    ui->lightList->blockSignals(true);
    ui->lightList->clear();

    const std::vector<SceneLight*> lights = core->sceneLights();

    for (SceneLight* l : lights)
    {
        if (!l)
            continue;

        // Assumes SceneLight has name() -> std::string_view (or similar).
        const QString name = to_qstring(l->name());

        QListWidgetItem* item = new QListWidgetItem(name);
        ui->lightList->addItem(item);
    }

    ui->lightList->blockSignals(false);

    restoreSelectionByName(prevSel);

    // If nothing selected, select the first item if it exists (optional but nice UX)
    if (ui->lightList->currentRow() < 0 && ui->lightList->count() > 0)
        ui->lightList->setCurrentRow(0);
}

QString LightsEditorDialog::currentSelectedName() const
{
    if (!ui || !ui->lightList)
        return {};

    if (QListWidgetItem* it = ui->lightList->currentItem())
        return it->text();

    return {};
}

void LightsEditorDialog::restoreSelectionByName(const QString& name)
{
    if (!ui || !ui->lightList)
        return;

    const QString key = name.trimmed();
    if (key.isEmpty())
        return;

    for (int i = 0; i < ui->lightList->count(); ++i)
    {
        QListWidgetItem* it = ui->lightList->item(i);
        if (!it)
            continue;

        if (QString::compare(it->text(), key, Qt::CaseInsensitive) == 0)
        {
            ui->lightList->setCurrentRow(i);
            return;
        }
    }
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

        const int marginW =
            (layout() ? (layout()->contentsMargins().left() + layout()->contentsMargins().right()) : 0) +
            (ui->splitterMain ? (ui->splitterMain->contentsMargins().left() + ui->splitterMain->contentsMargins().right()) : 0);

        const int newW = ui->rightPanel->sizeHint().width() + marginW + 2;
        setFixedWidth(std::max(newW, minimumSizeHint().width()));
        resize(width(), height());
    }
    else
    {
        ui->leftPanel->setVisible(true);

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
