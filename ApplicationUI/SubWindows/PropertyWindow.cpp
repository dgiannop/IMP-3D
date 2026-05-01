#include "PropertyWindow.hpp"

#include <Core.hpp>
#include <QLayout>
#include <QSizePolicy>

#include "PropertyItem.hpp"
#include "SubWindows/ui_PropertyWindow.h"

PropertyWindow::PropertyWindow(QWidget* parent) : SubWindowBase(parent),
                                                  ui(new Ui::PropertyWindow)
{
    ui->setupUi(this);

    setWindowTitle("Tool Properties");

    ui->verticalLayout->setAlignment(Qt::AlignTop);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    if (layout())
        layout()->setSizeConstraint(QLayout::SetMinimumSize);

    setMinimumWidth(220);

    resize(width(), sizeHint().height());
}

void PropertyWindow::resizeToContents()
{
    if (ui->verticalLayout)
    {
        ui->verticalLayout->invalidate();
        ui->verticalLayout->activate();
    }

    if (layout())
    {
        layout()->invalidate();
        layout()->activate();
    }

    resize(width(), sizeHint().height());
}

void PropertyWindow::idleEvent(Core* core)
{
    if (core->toolPropertyGroupChanged())
    {
        while (QLayoutItem* item = ui->verticalLayout->takeAt(0))
        {
            if (QWidget* widget = item->widget())
                widget->deleteLater();

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

        resizeToContents();
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
                item->updateUiValue();
        }

        resizeToContents();
    }
}

PropertyWindow::~PropertyWindow()
{
    delete ui;
}
