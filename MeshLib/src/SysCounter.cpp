#include "SysCounter.hpp"

#include <cassert>

void SysCounter::change()
{
    ++m_value;

    for (const SysCounterPtr& parent : m_parents)
    {
        if (parent)
        {
            parent->change();
        }
    }
}

void SysCounter::addParent(const SysCounterPtr& parent)
{
    assert(parent && "SysCounter::addParent called with null parent");

    // Optional: prevent duplicates
    for (const SysCounterPtr& p : m_parents)
    {
        if (p == parent)
            return;
    }

    m_parents.push_back(parent);
}

uint64_t SysCounter::value() const noexcept
{
    return m_value;
}

/// ---------------------------------------------
/// SysMonitor implementation
/// ---------------------------------------------

SysMonitor::SysMonitor(SysCounterPtr counter) : m_counter{std::move(counter)}, m_prevValue{0}
{
    if (m_counter)
    {
        // Start dirty for now. Later add a m_dirty flag
        // m_prevValue = m_counter->value();
    }
}

bool SysMonitor::changed() noexcept
{
    if (m_counter && m_counter->value() != m_prevValue)
    {
        m_prevValue = m_counter->value();
        return true;
    }
    return false;
}
