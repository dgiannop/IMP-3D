#ifndef PROPERTYITEM_HPP
#define PROPERTYITEM_HPP

#include <Property.hpp>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>
#include <glm/ext/vector_int3.hpp>
#include <limits>

class PropertyItem : public QWidget
{
    Q_OBJECT

public:
    PropertyItem(PropertyBase* coreProperty, QWidget* editor, QWidget* parent = nullptr) :
        QWidget(parent),
        m_editor(editor),
        coreProp(coreProperty)
    {
        m_label = new QLabel(QString::fromStdString(coreProp->name()), this);
        m_label->setFixedHeight(24);

        m_hbox = new QHBoxLayout(this);
        m_hbox->addWidget(m_label);
        m_hbox->addWidget(m_editor);
        m_hbox->setStretch(1, 0);
        m_editor->setFixedWidth(90);
        m_editor->setFixedHeight(24);
        m_hbox->setContentsMargins(0, 0, 0, 0);
        m_hbox->setSpacing(1);
        setLayout(m_hbox);
        setFixedHeight(24);

        // Wire editor behavior based on type
        if (coreProperty->type() == PropertyType::INT)
        {
            auto* sb = qobject_cast<QSpinBox*>(m_editor);
            sb->setMinimum(*static_cast<int*>(coreProp->min()));
            sb->setMaximum(*static_cast<int*>(coreProp->max()));
            sb->setAccelerated(true);

            // Optional step hint (useful for page-like stepping)
            if (coreProp->hasStep())
                sb->setSingleStep(static_cast<int>(coreProp->step()));
            // else default is 1

            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [=, this](int value) { coreProp->setValue(&value); });
        }
        else if (coreProperty->type() == PropertyType::INT_RO)
        {
            auto* sb = qobject_cast<QSpinBox*>(m_editor);
            sb->setReadOnly(true);
            sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
            sb->setMinimum(std::numeric_limits<int>::lowest());
            sb->setMaximum(std::numeric_limits<int>::max());
        }
        else if (coreProperty->type() == PropertyType::FLOAT)
        {
            auto* sb = qobject_cast<QDoubleSpinBox*>(m_editor);
            sb->setMinimum(*static_cast<float*>(coreProp->min()));
            sb->setMaximum(*static_cast<float*>(coreProp->max()));

            // Apply step/decimals hints if present
            const bool   hasStep = coreProp->hasStep();
            const double step    = hasStep ? coreProp->step() : 0.1;
            sb->setSingleStep(step);

            if (coreProp->decimals() >= 0)
                sb->setDecimals(coreProp->decimals());
            else
                sb->setDecimals(3);

            sb->setAccelerated(true);

            // NOTE: QDoubleSpinBox emits double; capture double and cast to float
            connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=, this](double value) {
                        float f = static_cast<float>(value);
                        coreProp->setValue(&f); });
        }
    }

    virtual void updateUiValue() = 0;

protected:
    PropertyBase* coreProp;
    QWidget*      m_editor;

private:
    QLabel*      m_label;
    QHBoxLayout* m_hbox;
};

// -------- Derived classes --------

class IntPropertyItem : public PropertyItem
{
public:
    IntPropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, new QSpinBox(), parent)
    {
    }

    virtual void updateUiValue() override
    {
        qobject_cast<QSpinBox*>(m_editor)->setValue(*static_cast<int*>(coreProp->value()));
    }
};

class FloatPropertyItem : public PropertyItem
{
public:
    FloatPropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, new QDoubleSpinBox(), parent)
    {
    }

    virtual void updateUiValue() override
    {
        qobject_cast<QDoubleSpinBox*>(m_editor)->setValue(*static_cast<float*>(coreProp->value()));
    }
};

class DoublePropertyItem : public PropertyItem
{
public:
    DoublePropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, new QDoubleSpinBox(), parent)
    {
    }

    virtual void updateUiValue() override
    {
        qobject_cast<QDoubleSpinBox*>(m_editor)->setValue(*static_cast<double*>(coreProp->value()));
    }
};

class StringPropertyItem : public PropertyItem
{
public:
    StringPropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, new QLineEdit(), parent)
    {
    }

    virtual void updateUiValue() override
    {
        qobject_cast<QLineEdit*>(m_editor)->setText(*static_cast<QString*>(coreProp->value()));
    }
};

class BoolPropertyItem : public PropertyItem
{
public:
    BoolPropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, new QWidget(), parent)
    {
        QWidget* container  = m_editor;
        auto*    hboxLayout = new QHBoxLayout(container);
        hboxLayout->setContentsMargins(0, 0, 0, 0);
        hboxLayout->setSpacing(4);

        m_onButton  = new QPushButton("On");
        m_offButton = new QPushButton("Off");

        m_onButton->setCheckable(true);
        m_offButton->setCheckable(true);
        m_onButton->setFixedHeight(24);
        m_offButton->setFixedHeight(24);

        auto* buttonGroup = new QButtonGroup(container);
        buttonGroup->setExclusive(true);
        buttonGroup->addButton(m_onButton);
        buttonGroup->addButton(m_offButton);

        hboxLayout->addWidget(m_onButton);
        hboxLayout->addWidget(m_offButton);
        container->setLayout(hboxLayout);

        syncUiToValue();

        connect(m_onButton, &QPushButton::clicked, this, [=, this] {
                    bool value = true;
                    coreProp->setValue(&value);
                    syncUiToValue(); });

        connect(m_offButton, &QPushButton::clicked, this, [=, this] {
                    bool value = false;
                    coreProp->setValue(&value);
                    syncUiToValue(); });
    }

    void updateUiValue() override
    {
        syncUiToValue();
    }

private:
    void syncUiToValue()
    {
        if (!coreProp || !coreProp->value())
            return;
        bool value = *static_cast<bool*>(coreProp->value());
        if (m_onButton)
            m_onButton->setChecked(value);
        if (m_offButton)
            m_offButton->setChecked(!value);
    }

    QPushButton* m_onButton  = nullptr;
    QPushButton* m_offButton = nullptr;
};

class AxisPropertyItem : public PropertyItem
{
public:
    AxisPropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, new QWidget(), parent)
    {
        QWidget* container = m_editor;
        auto*    layout    = new QHBoxLayout(container);

        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(1);

        m_xButton = new QPushButton("X");
        m_yButton = new QPushButton("Y");
        m_zButton = new QPushButton("Z");

        m_xButton->setCheckable(true);
        m_yButton->setCheckable(true);
        m_zButton->setCheckable(true);
        m_xButton->setFixedHeight(24);
        m_yButton->setFixedHeight(24);
        m_zButton->setFixedHeight(24);

        auto* buttonGroup = new QButtonGroup(container);
        buttonGroup->setExclusive(true);
        buttonGroup->addButton(m_xButton);
        buttonGroup->addButton(m_yButton);
        buttonGroup->addButton(m_zButton);

        layout->addWidget(m_xButton);
        layout->addWidget(m_yButton);
        layout->addWidget(m_zButton);
        container->setLayout(layout);

        syncUiToValue();

        connect(m_xButton, &QPushButton::clicked, this, [=, this] {
            glm::ivec3 value(1, 0, 0);
            coreProp->setValue(&value);
            syncUiToValue(); });

        connect(m_yButton, &QPushButton::clicked, this, [=, this] {
            glm::ivec3 value(0, 1, 0);
            coreProp->setValue(&value);
            syncUiToValue(); });

        connect(m_zButton, &QPushButton::clicked, this, [=, this] {
            glm::ivec3 value(0, 0, 1);
            coreProp->setValue(&value);
            syncUiToValue(); });
    }

    void updateUiValue() override
    {
        syncUiToValue();
    }

private:
    QPushButton* m_xButton = nullptr;
    QPushButton* m_yButton = nullptr;
    QPushButton* m_zButton = nullptr;

    void syncUiToValue()
    {
        if (!coreProp || !coreProp->value())
            return;
        glm::ivec3 value = *static_cast<glm::ivec3*>(coreProp->value());

        m_xButton->setChecked(value.x == 1);
        m_yButton->setChecked(value.y == 1);
        m_zButton->setChecked(value.z == 1);
    }
};

class ColorPropertyItem : public PropertyItem
{
public:
    ColorPropertyItem(PropertyBase* coreProp, QWidget* parent = nullptr) :
        PropertyItem(coreProp, createEditor(), parent),
        color(Qt::white)
    {
    }

    virtual void updateUiValue() override
    {
        // Implement if core property stores color; currently local preview only.
    }

private:
    QColor  color;
    QLabel* colorLabel = nullptr;

    QWidget* createEditor()
    {
        QWidget* editor       = new QWidget();
        auto*    editorLayout = new QHBoxLayout(editor);

        colorLabel = new QLabel();
        colorLabel->setFixedSize(50, 20);
        updateColorLabel();

        QPushButton* colorButton = new QPushButton("Pick Color");
        connect(colorButton, &QPushButton::clicked, this, &ColorPropertyItem::openColorPicker);

        editorLayout->addWidget(colorLabel);
        editorLayout->addWidget(colorButton);
        editorLayout->setContentsMargins(0, 0, 0, 0);

        return editor;
    }

    void updateColorLabel()
    {
        QPalette palette = colorLabel->palette();
        palette.setColor(QPalette::Window, color);
        colorLabel->setAutoFillBackground(true);
        colorLabel->setPalette(palette);
    }

    void openColorPicker()
    {
        QColor newColor = QColorDialog::getColor(color, this, "Select Color");
        if (newColor.isValid())
        {
            color = newColor;
            updateColorLabel();
        }
    }
};

#endif // PROPERTYITEM_HPP
