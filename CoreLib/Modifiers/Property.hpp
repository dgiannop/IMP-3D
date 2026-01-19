#pragma once

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "CoreTypes.hpp"

class PropertyBase
{
public:
    virtual ~PropertyBase()
    {
    }

    virtual const std::string& name() const = 0;
    virtual PropertyType       type() const = 0;

    virtual bool changed() = 0;

    // raw pointers to underlying storage (kept for compatibility)
    virtual void* value() = 0;
    virtual void* min()   = 0;
    virtual void* max()   = 0;

    virtual void setValue(void* val) = 0;

    // --- Editor hints (optional) ---
    virtual bool   hasStep() const  = 0; // true when step() is set
    virtual double step() const     = 0; // NaN if unset
    virtual int    decimals() const = 0; // -1 if unset
};

template<typename T>
class Property : public PropertyBase
{
public:
    // Value-only (no min/max); optional step/decimals
    explicit Property(const std::string& name, PropertyType type, T* value, double step = std::numeric_limits<double>::quiet_NaN(), int decimals = -1) :
        m_name(name),
        m_type(type),
        m_currVal(value),
        m_prevVal(*value),
        m_min(std::numeric_limits<T>::lowest()),
        m_max(std::numeric_limits<T>::max()),
        m_step(step),
        m_decimals(decimals),
        m_changed(true)
    {
    }

    // Value with min/max; optional step/decimals
    explicit Property(const std::string& name, PropertyType type, T* value, T min, T max, double step = std::numeric_limits<double>::quiet_NaN(), int decimals = -1) :
        m_name(name),
        m_type(type),
        m_currVal(value),
        m_prevVal(*value),
        m_min(min),
        m_max(max),
        m_step(step),
        m_decimals(decimals),
        m_changed(true)
    {
    }

    virtual const std::string& name() const override
    {
        return m_name;
    }

    virtual PropertyType type() const override
    {
        return m_type;
    }

    virtual void* value() override
    {
        return m_currVal;
    }

    virtual void* min() override
    {
        return &m_min;
    }

    virtual void* max() override
    {
        return &m_max;
    }

    virtual bool changed() override
    {
        if (m_prevVal != *m_currVal)
        {
            m_prevVal = *m_currVal;
            return true;
        }
        if (m_changed)
        {
            m_changed = false;
            return true;
        }
        return false;
    }

    virtual void setValue(void* val) override
    {
        m_prevVal = *m_currVal = *static_cast<T*>(val);
        m_changed              = true;
    }

    // Editor hints (no used ATM)
    virtual bool hasStep() const override
    {
        return !std::isnan(m_step);
    }

    virtual double step() const override
    {
        return m_step; // NaN => unset
    }

    virtual int decimals() const override
    {
        return m_decimals; // -1 => unset
    }

private:
    std::string  m_name;
    PropertyType m_type;

    T* m_currVal;
    T  m_prevVal{};
    T  m_min{};
    T  m_max{};

    double m_step;     // NaN = not specified
    int    m_decimals; // -1 = not specified

    bool m_changed;
};

class PropertyGroup
{
public:
    PropertyGroup() :
        m_groupChanged{true}
    {
    }

    // Value-only property; optional step/decimals
    template<typename T>
    void addProperty(const std::string& name, PropertyType ptType, T* value, double step = std::numeric_limits<double>::quiet_NaN(), int decimals = -1)
    {
        m_properties.push_back(std::make_unique<Property<T>>(name, ptType, value, step, decimals));
        m_groupChanged = true;
    }

    // Property with min/max; optional step/decimals
    template<typename T>
    void addProperty(const std::string& name, PropertyType ptType, T* value, T min, T max, double step = std::numeric_limits<double>::quiet_NaN(), int decimals = -1)
    {
        m_properties.push_back(std::make_unique<Property<T>>(name, ptType, value, min, max, step, decimals));
        m_groupChanged = true;
    }

    const std::vector<std::unique_ptr<PropertyBase>>& properties() const
    {
        return m_properties;
    }

    bool propertyGroupChanged()
    {
        if (m_groupChanged)
        {
            m_groupChanged = false;
            return true;
        }
        return false;
    }

    bool propertyValuesChanged()
    {
        bool result = false;
        for (auto& prop : m_properties)
        {
            if (prop->changed())
            {
                result = true;
            }
        }
        return result;
    }

private:
    std::vector<std::unique_ptr<PropertyBase>> m_properties;
    bool                                       m_groupChanged;
};
