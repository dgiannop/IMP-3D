#include "PropertyWindow.hpp"

#include <qmainwindow.h>

#include <Core.hpp>

#include "PropertyItem.hpp"
#include "SubWindows/ui_PropertyWindow.h"

PropertyWindow::PropertyWindow(QWidget* parent) : SubWindowBase(parent),
                                                  ui(new Ui::PropertyWindow)
{
    ui->setupUi(this);
    // resize(220, 300);
    // setMinimumSize(200, 300);
    // setWindowTitleAndSize("Tool Properties", 240, 350);
    // ui->verticalLayout->setAlignment(Qt::AlignTop);

    resize(220, 300);
    setWindowTitle("Tool Properties");
    setMinimumSize(200, 300);
    setMaximumSize(240, 350);
    ui->verticalLayout->setAlignment(Qt::AlignTop);
}

void PropertyWindow::idleEvent(Core* core)
{
    if (core->toolPropertyGroupChanged())
    {
        while (QLayoutItem* item = ui->verticalLayout->takeAt(0))
        {
            if (QWidget* widget = item->widget())
            {
                widget->deleteLater();
            }
            delete item;
        }

        for (const auto& property : core->toolProperties())
        {
            if (property->type() == PropertyType::INT)
            {
                ui->verticalLayout->addWidget(new IntPropertyItem(property.get(), this));
            }
            else if (property->type() == PropertyType::FLOAT)
            {
                ui->verticalLayout->addWidget(new FloatPropertyItem(property.get(), this));
            }
            else if (property->type() == PropertyType::BOOL)
            {
                ui->verticalLayout->addWidget(new BoolPropertyItem(property.get(), this));
            }
            else if (property->type() == PropertyType::AXIS)
            {
                ui->verticalLayout->addWidget(new AxisPropertyItem(property.get(), this));
            }
        }
    }

    if (core->toolPropertyValuesChanged())
    {
        for (int i = 0; i < ui->verticalLayout->count(); ++i)
        {
            QLayoutItem* layoutItem = ui->verticalLayout->itemAt(i);
            if (!layoutItem)
                continue;

            QWidget* widget = layoutItem->widget();
            if (!widget)
                continue;

            if (PropertyItem* item = qobject_cast<PropertyItem*>(widget))
            {
                item->updateUiValue();
            }
        }
    }
}

PropertyWindow::~PropertyWindow()
{
    delete ui;
}
