#pragma once

#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @brief Base class for undoable/redoable actions.
 *
 * A HistoryAction is executed with a user-provided context pointer (void* data).
 * Concrete actions implement undo() and redo().
 */
class HistoryAction
{
public:
    virtual ~HistoryAction() noexcept = default;

    /// Undo this action using the provided context pointer.
    virtual void undo(void* data) = 0;

    /// Redo this action using the provided context pointer.
    virtual void redo(void* data) = 0;

    /**
     * @brief Optional barrier hook.
     *
     * Historically used by some actions to release temporary resources or finalize
     * state. Default is a no-op.
     */
    virtual void freeze()
    {
    }
};

/**
 * @brief A stack/timeline of HistoryAction objects, itself usable as a HistoryAction (nesting).
 *
 * History maintains a linear timeline:
 * - insert() appends a new action and truncates any redo tail.
 * - undo_step()/redo_step() walk one action at a time.
 * - undo()/redo() walk all the way to the beginning/end.
 *
 * @note m_index is the index of the last *applied* action, or -1 if none applied.
 */
class History final : public HistoryAction
{
public:
    /**
     * @param idata            Context pointer passed to all child actions.
     * @param externalBusyPtr  Optional pointer mirrored with the busy flag.
     *                         Useful when multiple History objects share a single guard.
     */
    explicit History(void* idata, bool* externalBusyPtr = nullptr);
    ~History() override;

    // Treat this History as a single atomic step (nested use)
    void undo(void* /*unused*/) override
    {
        undo();
    }
    void redo(void* /*unused*/) override
    {
        redo();
    }

    /// Undo all actions back to the beginning (uses stored context pointer).
    void undo();

    /// Redo all actions forward to the end (uses stored context pointer).
    void redo();

    /// Undo a single action. @return true if something was undone.
    [[nodiscard]] bool undo_step();

    /// Redo a single action. @return true if something was redone.
    [[nodiscard]] bool redo_step();

    /**
     * @brief Insert another History as a single nested action (convenience overload).
     *
     * Equivalent to insert(std::unique_ptr<HistoryAction>(std::move(new_history))).
     */
    void insert(std::unique_ptr<History> new_history);

    /**
     * @brief Insert an action into the timeline.
     *
     * Truncates any redo tail and appends the action as the new "last applied".
     *
     * @warning Must not be called while history is busy (during undo/redo playback).
     */
    void insert(std::unique_ptr<HistoryAction> new_action);

    /// Clear all actions and reset the timeline.
    void clear();

    // ------------------------------------------------------------
    // Convenience: construct + insert, returning raw pointer to T.
    //
    // Hybrid-friendly: lets you fill fields after emplace().
    // ------------------------------------------------------------

    /**
     * @brief Construct an action of type T, insert it, and return a raw pointer to it.
     *
     * This is convenient for "hybrid" call sites where you want to create + insert
     * an action, then populate its fields, without writing make_unique + insert.
     *
     * @code
     * if (!data->history->is_busy())
     * {
     *     auto* undo   = data->history->emplace<UndoSelectMapVert>();
     *     undo->index  = vert_index;
     *     undo->map    = map;
     *     undo->select = select;
     * } // <- done with history action, do other stuff now
     * @endcode
     */
    template<typename T, typename... Args>
    T* emplace(Args&&... args)
    {
        static_assert(std::is_base_of_v<HistoryAction, T>,
                      "History::emplace<T>: T must derive from HistoryAction");

        auto action = std::make_unique<T>(std::forward<Args>(args)...);
        T*   raw    = action.get();

        insert(std::move(action));
        return raw;
    }

    /// @return true if there is at least one action to undo.
    [[nodiscard]] bool can_undo() const noexcept;

    /// @return true if there is at least one action to redo.
    [[nodiscard]] bool can_redo() const noexcept;

    /// @return true while this History (or a linked History) is replaying.
    [[nodiscard]] bool is_busy() const noexcept;

    /**
     * @brief Barrier kept for compatibility.
     */
    void freeze() override
    {
    }

private:
    void set_busy(bool busy) noexcept;

private:
    std::vector<std::unique_ptr<HistoryAction>> m_actions;

    int   m_index{-1};             ///< Index of last applied action, or -1 if none applied.
    void* m_data{nullptr};         ///< Context pointer passed to child actions.
    bool  m_busyFlag{false};       ///< Internal replay guard.
    bool* m_externalBusy{nullptr}; ///< Optional mirrored guard (shared across histories).
};
