#include "History.hpp"

// ------------------------------------------------------------

History::History(void* idata, bool* externalBusyPtr) :
    m_actions(),
    m_index(-1),
    m_data(idata),
    m_busyFlag(false),
    m_externalBusy(externalBusyPtr)
{
}

History::~History()
{
    clear();
}

// ------------------------------------------------------------

void History::set_busy(bool busy) noexcept
{
    m_busyFlag = busy;
    if (m_externalBusy)
        *m_externalBusy = busy;
}

bool History::is_busy() const noexcept
{
    // Busy if THIS history is replaying, OR if any linked history
    // has set the external guard (e.g., the released mesh history).
    return m_busyFlag || (m_externalBusy && *m_externalBusy);
}

bool History::can_undo() const noexcept
{
    return m_index >= 0;
}

bool History::can_redo() const noexcept
{
    return (m_index + 1) < static_cast<int>(m_actions.size());
}

// ------------------------------------------------------------

void History::insert(std::unique_ptr<History> new_history)
{
    insert(std::unique_ptr<HistoryAction>(std::move(new_history)));
}

void History::insert(std::unique_ptr<HistoryAction> new_action)
{
    assert(new_action && "History::insert received null action");
    assert(!is_busy() && "History::insert called while undoing/redoing");

    // Drop redo tail if user branches new edits
    while ((m_index + 1) < static_cast<int>(m_actions.size()))
        m_actions.pop_back();

    // After dropping tail, the next insert becomes the new "last applied"
    m_index = static_cast<int>(m_actions.size());
    m_actions.push_back(std::move(new_action));
}

void History::clear()
{
    // Keep historical leniency: allow clear() even if busy at shutdown.
    // But ensure the external guard does not remain stuck "true".
    if (is_busy())
    {
        m_busyFlag = false;
        if (m_externalBusy)
            *m_externalBusy = false;
    }

    m_actions.clear();
    m_index = -1;
}

// ------------------------------------------------------------

void History::undo()
{
    if (!can_undo())
        return;

    set_busy(true);
    while (can_undo())
    {
        assert(m_index >= 0 && m_index < static_cast<int>(m_actions.size()));
        m_actions[m_index]->undo(m_data);
        --m_index;
    }
    set_busy(false);
}

void History::redo()
{
    if (!can_redo())
        return;

    set_busy(true);
    while (can_redo())
    {
        const int next = m_index + 1;
        assert(next >= 0 && next < static_cast<int>(m_actions.size()));
        m_actions[next]->redo(m_data);
        ++m_index;
    }
    set_busy(false);
}

// ------------------------------------------------------------

bool History::undo_step()
{
    if (!can_undo())
        return false;

    set_busy(true);
    assert(m_index >= 0 && m_index < static_cast<int>(m_actions.size()));
    m_actions[m_index]->undo(m_data);
    --m_index;
    set_busy(false);
    return true;
}

bool History::redo_step()
{
    if (!can_redo())
        return false;

    set_busy(true);
    const int next = m_index + 1;
    assert(next >= 0 && next < static_cast<int>(m_actions.size()));
    m_actions[next]->redo(m_data);
    ++m_index;
    set_busy(false);
    return true;
}
