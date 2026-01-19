#ifndef SYS_COUNTER_HPP_INCLUDED
#define SYS_COUNTER_HPP_INCLUDED

#include <cstdint>
#include <memory>
#include <vector>

/**
 * @brief Shared pointer to a SysCounter.
 */
class SysCounter;
using SysCounterPtr = std::shared_ptr<SysCounter>;

/**
 * @brief Tracks changes using an internal version counter.
 *
 * SysCounter is a lightweight utility for tracking state changes.
 * Each call to `change()` increments the internal counter. If one or more
 * parent counters are set, the change will propagate to all parent counters
 * as well.
 */
class SysCounter
{
public:
    /**
     * @brief Default constructor.
     */
    SysCounter() = default;

    /**
     * @brief Increments the change counter and notifies all parent counters.
     */
    void change();

    /**
     * @brief Adds a parent counter that will also be updated when this one changes.
     * @param parent The parent counter to notify.
     */
    void addParent(const SysCounterPtr& parent);

    /**
     * @brief Returns the current counter value.
     * @return The internal version number.
     */
    [[nodiscard]] uint64_t value() const noexcept;

private:
    /// Parent counters that receive propagated change notifications.
    std::vector<SysCounterPtr> m_parents;

    /// Internal version counter.
    uint64_t m_value{0};
};

/**
 * @brief Monitors a SysCounter for modifications over time.
 *
 * Stores the value of a SysCounter at the time of construction and
 * can be queried to detect if the counter has changed since.
 */
class SysMonitor
{
public:
    /**
     * @brief Constructs a monitor for the given counter.
     * @param counter The counter to monitor.
     */
    explicit SysMonitor(SysCounterPtr counter);

    /**
     * @brief Checks if the counter has changed since the last query.
     * @return True if the counter's value is different from when last queried.
     */
    [[nodiscard]] bool changed() noexcept;

private:
    SysCounterPtr m_counter;
    uint64_t      m_prevValue;
};

#endif // SYS_COUNTER_HPP_INCLUDED
