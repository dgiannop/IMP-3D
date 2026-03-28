#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include <vector>

/**
 * @brief Stable-index slot-map container.
 *
 * A flat array where removed elements leave "holes" that are reused on the
 * next insert, keeping all existing indices permanently stable. Useful for
 * mesh topology data where vertices/edges/faces must be addressable by a
 * persistent integer index even as elements are added and removed.
 *
 * Iteration over all occupied elements should be done via valid_indices():
 *
 *   for (int32_t i : mesh.valid_indices())
 *       process(mesh[i]);
 *
 * The raw begin()/end() iterators span the full slot array including holes
 * and are provided for algorithms that handle holes themselves (e.g. direct
 * index-based loops with an occupancy check).
 *
 * @tparam T  Element type.
 */
template<typename T>
class HoleList
{
public:
    using value_type             = T;
    using reference              = T&;
    using const_reference        = const T&;
    using pointer                = T*;
    using const_pointer          = const T*;
    using size_type              = int32_t;
    using iterator               = typename std::vector<T>::iterator;
    using const_iterator         = typename std::vector<T>::const_iterator;
    using reverse_iterator       = typename std::vector<T>::reverse_iterator;
    using const_reverse_iterator = typename std::vector<T>::const_reverse_iterator;

    HoleList()                               = default;
    HoleList(const HoleList&)                = default;
    HoleList(HoleList&&) noexcept            = default;
    HoleList& operator=(const HoleList&)     = default;
    HoleList& operator=(HoleList&&) noexcept = default;
    ~HoleList()                              = default;

    // ------------------------------------------------------------------
    // capacity
    // ------------------------------------------------------------------

    /// @return The number of occupied (non-hole) elements.
    [[nodiscard]] size_type size() const noexcept
    {
        return m_size;
    }

    /// @return The total number of slots in the backing array, including holes.
    /// Use size() for the count of live elements.
    [[nodiscard]] size_type slot_count() const noexcept
    {
        return static_cast<size_type>(m_elements.size());
    }

    /// @return The number of holes (free slots available for reuse).
    [[nodiscard]] size_type hole_count() const noexcept
    {
        return static_cast<size_type>(m_freeStack.size());
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_size == 0;
    }

    /// Reserve backing storage for at least `amount` total slots.
    /// Call before bulk inserts to avoid incremental reallocations.
    void reserve(size_type amount)
    {
        m_elements.reserve(static_cast<std::size_t>(amount));
        m_freeStack.reserve(static_cast<std::size_t>(amount) / 4);
    }

    // ------------------------------------------------------------------
    // element access
    // ------------------------------------------------------------------

    /// Access element at index. Index must refer to an occupied slot.
    /// NOTE: begin()/end() span the full slot array including holes.
    /// Use valid_indices() to iterate only occupied slots.
    [[nodiscard]] reference operator[](size_type index) noexcept
    {
        assert(index >= 0 && index < slot_count() && "HoleList: index out of range");
        return m_elements[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] const_reference operator[](size_type index) const noexcept
    {
        assert(index >= 0 && index < slot_count() && "HoleList: index out of range");
        return m_elements[static_cast<std::size_t>(index)];
    }

    // ------------------------------------------------------------------
    // iterators (full slot range, includes holes)
    // ------------------------------------------------------------------

    [[nodiscard]] iterator       begin() noexcept { return m_elements.begin(); }
    [[nodiscard]] iterator       end() noexcept { return m_elements.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return m_elements.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return m_elements.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return m_elements.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return m_elements.cend(); }

    [[nodiscard]] reverse_iterator       rbegin() noexcept { return m_elements.rbegin(); }
    [[nodiscard]] reverse_iterator       rend() noexcept { return m_elements.rend(); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return m_elements.rbegin(); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return m_elements.rend(); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return m_elements.crbegin(); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return m_elements.crend(); }

    // ------------------------------------------------------------------
    // modifiers
    // ------------------------------------------------------------------

    /// Copy-insert. Only available when T is copy-constructible.
    /// Calls the copy overload of insert_impl.
    size_type insert(const T& element)
        requires std::copy_constructible<T>
    {
        return insert_impl(element);
    }

    /// Move-insert. Called for rvalues and std::move'd lvalues.
    size_type insert(T&& element)
        requires std::move_constructible<T>
    {
        return insert_impl(std::move(element));
    }

    /// Construct element in place directly into the next available slot.
    template<typename... Args>
    size_type emplace(Args&&... args)
    {
        size_type index;
        if (m_freeStack.empty())
        {
            index = static_cast<size_type>(m_elements.size());
            m_elements.emplace_back(std::forward<Args>(args)...);
        }
        else
        {
            index = m_freeStack.back();
            m_freeStack.pop_back();
            m_freeSet.erase(index);
            // Slot still holds a live T object (just logically vacated) so
            // assign rather than placement-new to avoid a double-live object.
            m_elements[static_cast<std::size_t>(index)] = T(std::forward<Args>(args)...);
        }
        ++m_size;
        m_dirty = true;
        return index;
    }

    void remove(size_type index)
    {
        assert(index >= 0 && index < slot_count() && "HoleList: remove index out of range");
        assert(m_freeSet.count(index) == 0 && "HoleList: double-remove detected"); // O(1)

        --m_size;
        m_freeStack.push_back(index);
        m_freeSet.insert(index);
        m_dirty = true;
    }

    void clear() noexcept
    {
        m_elements.clear();
        m_freeStack.clear();
        m_freeSet.clear();
        m_cachedValidIndices.clear();
        m_size  = 0;
        m_dirty = false; // cache is trivially valid (empty) after clear
    }

    void swap(HoleList& other) noexcept
    {
        using std::swap;
        swap(m_elements, other.m_elements);
        swap(m_freeStack, other.m_freeStack);
        swap(m_freeSet, other.m_freeSet);
        swap(m_cachedValidIndices, other.m_cachedValidIndices);
        swap(m_size, other.m_size);
        swap(m_dirty, other.m_dirty);
    }

    // ------------------------------------------------------------------
    // valid index cache
    // ------------------------------------------------------------------

    /// @return A list of indices of all occupied (non-hole) slots.
    ///
    /// The result is cached and only rebuilt when the container is modified.
    /// Uses m_freeSet for O(1) hole lookup per slot — no temporary allocations.
    /// m_freeStack insertion order is preserved so insert() always pops the
    /// same hole, keeping undo/redo index sequences deterministic.
    [[nodiscard]] const std::vector<size_type>& valid_indices() const
    {
        if (!m_dirty)
            return m_cachedValidIndices;

        m_cachedValidIndices.clear();
        m_cachedValidIndices.reserve(static_cast<std::size_t>(m_size));

        const size_type total = slot_count();
        for (size_type i = 0; i < total; ++i)
        {
            if (m_freeSet.count(i) == 0)
                m_cachedValidIndices.push_back(i);
        }

        m_dirty = false;
        return m_cachedValidIndices;
    }

    /// @return True if the slot at index is occupied (not a hole). O(1).
    [[nodiscard]] bool is_valid(size_type index) const noexcept
    {
        if (index < 0 || index >= slot_count())
            return false;
        return m_freeSet.count(index) == 0;
    }

private:
    template<typename U>
    size_type insert_impl(U&& element)
    {
        size_type index;
        if (m_freeStack.empty())
        {
            index = static_cast<size_type>(m_elements.size());
            m_elements.push_back(std::forward<U>(element));
        }
        else
        {
            index = m_freeStack.back();
            m_freeStack.pop_back();
            m_freeSet.erase(index);
            m_elements[static_cast<std::size_t>(index)] = std::forward<U>(element);
        }
        ++m_size;
        m_dirty = true;
        return index;
    }

    std::vector<T>                 m_elements;
    std::vector<size_type>         m_freeStack; ///< LIFO reuse order — must not be sorted
    std::unordered_set<size_type>  m_freeSet;   ///< O(1) hole lookup mirror of m_freeStack
    mutable std::vector<size_type> m_cachedValidIndices;
    mutable bool                   m_dirty = false;
    size_type                      m_size  = 0;
};
