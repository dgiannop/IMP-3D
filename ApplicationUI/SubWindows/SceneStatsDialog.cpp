#include "SceneStatsDialog.hpp"

#include "SubWindows/ui_SceneStatsDialog.h"

SceneStatsDialog::SceneStatsDialog(QWidget* parent) :
    SubWindowBase(parent),
    ui(new Ui::SceneStatsDialog)
{
    ui->setupUi(this);
}

SceneStatsDialog::~SceneStatsDialog()
{
    delete ui;
}

void SceneStatsDialog::idleEvent(Core* core)
{
    if (!core)
        return;

    const uint64_t stamp = core->sceneContentChangeStamp();
    if (stamp == m_lastStamp)
        return;

    m_lastStamp = stamp;

    const SceneStats s = core->sceneStats();

    ui->labelVertsValue->setText(QString::number(s.verts));
    ui->labelPolysValue->setText(QString::number(s.polys));
    ui->labelNormsValue->setText(QString::number(s.norms));
    ui->labelUvsValue->setText(QString::number(s.uvPos));
}
