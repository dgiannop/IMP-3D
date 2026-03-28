#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace un
{
    /// Signed integer type used for sizes and indices throughout this container.
    using sizeType = int32_t;

    /**
     * @brief Small-buffer-optimized vector (C++23).
     *
     * Stores up to @p N elements inline (stack) with no heap allocation.
     * Spills to the heap automatically when the inline capacity is exceeded,
     * and reverts to inline storage whenever the size drops back to ≤ N.
     *
     * Element lifetimes are managed via placement-new / explicit destructor
     * calls — @p T need not be default-constructible and no superfluous
     * constructions occur.
     *
     * @tparam T  Element type. Must be move-constructible for most operations.
     * @tparam N  Inline (fixed) capacity. May be 0 for a pure heap vector.
     */
    template<class T, sizeType N>
    class small_list
    {
        // ------------------------------------------------------------------
        // Internal storage
        // NOTE: m_fixed must be declared before m_ptr so that inline_ptr()
        // is valid when m_ptr is initialized (members init in declaration order).
        // ------------------------------------------------------------------

        /// Raw inline buffer — properly aligned, never default-constructed.
        alignas(T) std::byte m_fixed[sizeof(T) * (N > 0 ? N : 1)];

        T*       m_ptr;      ///< Active buffer: points to m_fixed (inline) or a heap allocation.
        sizeType m_size;     ///< Number of live elements.
        sizeType m_capacity; ///< Total slots in the active buffer.

        // ------------------------------------------------------------------
        // Private helpers
        // ------------------------------------------------------------------

        /// @return true if the container is currently using a heap buffer.
        [[nodiscard]] bool dynamic() const noexcept { return m_ptr != inline_ptr(); }

        /// @return Pointer to the start of the inline buffer.
        [[nodiscard]] T* inline_ptr() noexcept
        {
            return std::launder(reinterpret_cast<T*>(m_fixed));
        }

        /// @return Const pointer to the start of the inline buffer.
        [[nodiscard]] const T* inline_ptr() const noexcept
        {
            return std::launder(reinterpret_cast<const T*>(m_fixed));
        }

        /**
         * @brief Allocate a raw (uninitialized) buffer for @p cap elements.
         * @param cap Number of element slots to allocate.
         * @return Pointer to the raw allocation.
         */
        [[nodiscard]] static T* alloc(sizeType cap)
        {
            return static_cast<T*>(::operator new(static_cast<std::size_t>(cap) * sizeof(T)));
        }

        /**
         * @brief Free a buffer previously returned by alloc().
         * @param p Pointer to the buffer. Must not be the inline buffer.
         */
        static void dealloc(T* p) noexcept { ::operator delete(p); }

        /**
         * @brief Destroy all elements in [@p first, @p last).
         *
         * Compiles to a no-op for trivially destructible @p T.
         */
        static void destroy_range(T* first, T* last) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
                for (; first != last; ++first)
                    std::destroy_at(first);
        }

        /**
         * @brief Relocate @p n elements from @p src into uninitialized @p dst.
         *
         * For trivially copyable @p T this compiles to a single memcpy.
         * For non-trivial @p T: move-constructs into @p dst, then destroys
         * the source range.
         */
        static void relocate(T* dst, T* src, sizeType n) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memcpy(dst, src, static_cast<std::size_t>(n) * sizeof(T));
            }
            else
            {
                for (sizeType i = 0; i < n; ++i)
                    std::construct_at(dst + i, std::move(src[i]));
                destroy_range(src, src + n);
            }
        }

        /**
         * @brief Shift elements left to close a gap, using memmove for trivial types.
         *
         * Moves elements [idx+count .. m_size) left by @p count positions.
         * For trivially copyable @p T compiles to a single memmove.
         * Preserves element order — suitable for erase operations on geometry data.
         *
         * @param idx   First position to overwrite.
         * @param count Number of positions to shift left.
         */
        void shift_left(sizeType idx, sizeType count) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memmove(
                    m_ptr + idx,
                    m_ptr + idx + count,
                    static_cast<std::size_t>(m_size - idx - count) * sizeof(T));
            }
            else
            {
                for (sizeType i = idx; i + count < m_size; ++i)
                    m_ptr[i] = std::move(m_ptr[i + count]);
            }
        }

        /**
         * @brief Shift elements right to open a gap, using memmove for trivial types.
         *
         * Opens a gap of one slot at @p idx by moving elements [idx .. m_size)
         * one position to the right. Caller must have ensured capacity.
         *
         * @param idx Position at which to open the gap.
         */
        void shift_right(sizeType idx) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>)
        {
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memmove(
                    m_ptr + idx + 1,
                    m_ptr + idx,
                    static_cast<std::size_t>(m_size - idx) * sizeof(T));
            }
            else
            {
                // Construct the last slot from the element before it, then
                // move-assign the rest backward.
                std::construct_at(m_ptr + m_size, std::move(m_ptr[m_size - 1]));
                std::move_backward(m_ptr + idx, m_ptr + m_size - 1, m_ptr + m_size);
                std::destroy_at(m_ptr + idx);
            }
        }

        /**
         * @brief Return to inline storage if size has dropped to ≤ N.
         *
         * Called after any operation that reduces m_size.
         * Has no effect on element ordering.
         */
        void try_revert_inline() noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            if (dynamic() && m_size <= static_cast<sizeType>(N))
            {
                T* il = inline_ptr();
                relocate(il, m_ptr, m_size);
                dealloc(m_ptr);
                m_ptr      = il;
                m_capacity = N > 0 ? N : 0;
            }
        }

        /**
         * @brief Grow the buffer to at least @p needed capacity (1.5× growth).
         *
         * Relocates existing elements into the new buffer.
         * Uses a minimum new capacity of 4 to avoid degenerate single-step
         * reallocation when starting from zero.
         *
         * @param needed Minimum required capacity after growth.
         */
        void grow(sizeType needed)
        {
            sizeType new_cap = std::max(m_capacity + m_capacity / 2, sizeType(4));
            if (new_cap < needed)
                new_cap = needed;

            T* new_buf = alloc(new_cap);
            relocate(new_buf, m_ptr, m_size);

            if (dynamic())
                dealloc(m_ptr);

            m_ptr      = new_buf;
            m_capacity = new_cap;
        }

    public:
        // ------------------------------------------------------------------
        // Standard typedefs
        // ------------------------------------------------------------------

        using value_type             = T;              ///< Element type.
        using reference              = T&;             ///< Reference to element.
        using const_reference        = const T&;       ///< Const reference to element.
        using pointer                = T*;             ///< Pointer to element.
        using const_pointer          = const T*;       ///< Const pointer to element.
        using size_type              = sizeType;       ///< Signed size/index type.
        using difference_type        = std::ptrdiff_t; ///< Iterator difference type.
        using iterator               = T*;             ///< Random-access iterator.
        using const_iterator         = const T*;       ///< Random-access const iterator.
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // ------------------------------------------------------------------
        // Construction / destruction
        // ------------------------------------------------------------------

        /// @brief Construct an empty container with inline storage.
        small_list() noexcept
            : m_ptr(inline_ptr()),
              m_size(0),
              m_capacity(N > 0 ? N : 0)
        {
        }

        /**
         * @brief Copy constructor. Copies all elements from @p other.
         * @param other Source container.
         */
        small_list(const small_list& other) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            reserve(other.m_size);
            for (sizeType i = 0; i < other.m_size; ++i)
                std::construct_at(m_ptr + i, other.m_ptr[i]);
            m_size = other.m_size;
        }

        /**
         * @brief Move constructor.
         *
         * O(1) when @p other is dynamic (steals heap buffer).
         * O(N) when @p other is inline (must relocate elements).
         *
         * @param other Source container. Left empty after the move.
         */
        small_list(small_list&& other) noexcept(std::is_nothrow_move_constructible_v<T>) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            if (other.dynamic())
            {
                m_ptr      = other.m_ptr;
                m_size     = other.m_size;
                m_capacity = other.m_capacity;

                other.m_ptr      = other.inline_ptr();
                other.m_size     = 0;
                other.m_capacity = N > 0 ? N : 0;
            }
            else
            {
                relocate(m_ptr, other.m_ptr, other.m_size);
                m_size       = other.m_size;
                other.m_size = 0;
            }
        }

        /**
         * @brief Fill constructor: @p n copies of @p val.
         * @param n   Number of elements.
         * @param val Value to copy into each element.
         */
        explicit small_list(size_type n, const T& val) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            assign(n, val);
        }

        /**
         * @brief Size constructor: @p n default-constructed elements.
         *
         * Only participates in overload resolution when @p T is
         * default-constructible, giving a clean compile error otherwise.
         *
         * @param n Number of elements to default-construct.
         */
        explicit small_list(size_type n)
            requires std::is_default_constructible_v<T>
            : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            reserve(n);
            for (size_type i = 0; i < n; ++i)
                std::construct_at(m_ptr + i);
            m_size = n;
        }

        /**
         * @brief Range constructor.
         * @tparam It Input iterator type.
         * @param first Begin of source range.
         * @param last  End of source range.
         */
        template<std::input_iterator It>
        small_list(It first, It last) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            assign(first, last);
        }

        /**
         * @brief Initializer-list constructor.
         * @param init Brace-enclosed list of values.
         */
        small_list(std::initializer_list<T> init) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            assign(init);
        }

        /// @brief Destructor. Destroys all elements and frees any heap buffer.
        ~small_list()
        {
            destroy_range(m_ptr, m_ptr + m_size);
            if (dynamic())
                dealloc(m_ptr);
        }

        // ------------------------------------------------------------------
        // Assignment
        // ------------------------------------------------------------------

        /**
         * @brief Copy-assignment operator.
         * @param other Source container.
         * @return *this.
         */
        small_list& operator=(const small_list& other)
        {
            if (this != &other)
            {
                clear();
                reserve(other.m_size);
                for (sizeType i = 0; i < other.m_size; ++i)
                    std::construct_at(m_ptr + i, other.m_ptr[i]);
                m_size = other.m_size;
            }
            return *this;
        }

        /**
         * @brief Move-assignment operator.
         * @param other Source container. Left empty after the move.
         * @return *this.
         */
        small_list& operator=(small_list&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            if (this != &other)
            {
                destroy_range(m_ptr, m_ptr + m_size);
                if (dynamic())
                    dealloc(m_ptr);

                m_ptr      = inline_ptr();
                m_size     = 0;
                m_capacity = N > 0 ? N : 0;

                if (other.dynamic())
                {
                    m_ptr      = other.m_ptr;
                    m_size     = other.m_size;
                    m_capacity = other.m_capacity;

                    other.m_ptr      = other.inline_ptr();
                    other.m_size     = 0;
                    other.m_capacity = N > 0 ? N : 0;
                }
                else
                {
                    relocate(m_ptr, other.m_ptr, other.m_size);
                    m_size       = other.m_size;
                    other.m_size = 0;
                }
            }
            return *this;
        }

        /**
         * @brief Initializer-list assignment.
         * @param init Brace-enclosed list of values.
         * @return *this.
         */
        small_list& operator=(std::initializer_list<T> init)
        {
            assign(init);
            return *this;
        }

        // ------------------------------------------------------------------
        // Span conversion
        // ------------------------------------------------------------------

        /// @brief Implicit conversion to a mutable span over all elements.
        constexpr operator std::span<T>() noexcept
        {
            return {m_ptr, static_cast<std::size_t>(m_size)};
        }

        /// @brief Implicit conversion to a const span over all elements.
        constexpr operator std::span<const T>() const noexcept
        {
            return {m_ptr, static_cast<std::size_t>(m_size)};
        }

        // ------------------------------------------------------------------
        // Assign overloads
        // ------------------------------------------------------------------

        /**
         * @brief Replace contents with @p n copies of @p val.
         * @param n   Number of elements.
         * @param val Value to fill with.
         */
        void assign(size_type n, const T& val)
        {
            clear();
            reserve(n);
            for (size_type i = 0; i < n; ++i)
                std::construct_at(m_ptr + i, val);
            m_size = n;
        }

        /**
         * @brief Replace contents with elements from [@p first, @p last).
         * @tparam It Input iterator type.
         */
        template<std::input_iterator It>
        void assign(It first, It last)
        {
            clear();
            if constexpr (std::forward_iterator<It>)
            {
                const auto n = static_cast<size_type>(std::distance(first, last));
                reserve(n);
                for (size_type i = 0; first != last; ++first, ++i)
                    std::construct_at(m_ptr + i, *first);
                m_size = n;
            }
            else
            {
                for (; first != last; ++first)
                    emplace_back(*first);
            }
        }

        /**
         * @brief Replace contents with an initializer list.
         * @param init Brace-enclosed list of values.
         */
        void assign(std::initializer_list<T> init)
        {
            assign(init.begin(), init.end());
        }

        // ------------------------------------------------------------------
        // Capacity
        // ------------------------------------------------------------------

        /// @return The compile-time inline capacity @p N.
        [[nodiscard]] size_type fixed_capacity() const noexcept { return N; }

        /// @return Current total capacity (inline or heap).
        [[nodiscard]] size_type capacity() const noexcept { return m_capacity; }

        /// @return true if the container holds no elements.
        [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

        /// @return Number of live elements.
        [[nodiscard]] size_type size() const noexcept { return m_size; }

        /**
         * @brief Ensure capacity for at least @p new_cap elements without growing elements.
         * @param new_cap Minimum desired capacity. No-op if already sufficient.
         */
        void reserve(size_type new_cap)
        {
            if (new_cap <= m_capacity)
                return;
            grow(new_cap);
        }

        /**
         * @brief Shrink capacity to size(), reverting to inline storage if possible.
         *
         * If size() ≤ N the heap buffer is freed and elements are moved back inline.
         * Otherwise a new minimal heap buffer is allocated.
         */
        void compact()
        {
            if (!dynamic())
                return;

            if (m_size <= N)
            {
                T* il = inline_ptr();
                relocate(il, m_ptr, m_size);
                dealloc(m_ptr);
                m_ptr      = il;
                m_capacity = N > 0 ? N : 0;
                return;
            }

            if (m_size == m_capacity)
                return;

            T* new_buf = alloc(m_size);
            relocate(new_buf, m_ptr, m_size);
            dealloc(m_ptr);
            m_ptr      = new_buf;
            m_capacity = m_size;
        }

        // ------------------------------------------------------------------
        // Element access
        // ------------------------------------------------------------------

        /// @return Reference to the first element. UB if empty.
        [[nodiscard]] reference front()
        {
            assert(!empty() && "front() on empty small_list");
            return m_ptr[0];
        }

        /// @return Const reference to the first element. UB if empty.
        [[nodiscard]] const_reference front() const
        {
            assert(!empty() && "front() on empty small_list");
            return m_ptr[0];
        }

        /// @return Reference to the last element. UB if empty.
        [[nodiscard]] reference back()
        {
            assert(!empty() && "back() on empty small_list");
            return m_ptr[m_size - 1];
        }

        /// @return Const reference to the last element. UB if empty.
        [[nodiscard]] const_reference back() const
        {
            assert(!empty() && "back() on empty small_list");
            return m_ptr[m_size - 1];
        }

        /**
         * @brief Subscript operator (unchecked in release builds).
         * @param n Zero-based index.
         * @return Reference to element at @p n.
         */
        [[nodiscard]] reference operator[](size_type n)
        {
            assert(n >= 0 && n < m_size && "small_list: index out of bounds");
            return m_ptr[n];
        }

        /**
         * @brief Const subscript operator (unchecked in release builds).
         * @param n Zero-based index.
         * @return Const reference to element at @p n.
         */
        [[nodiscard]] const_reference operator[](size_type n) const
        {
            assert(n >= 0 && n < m_size && "small_list: index out of bounds");
            return m_ptr[n];
        }

        /// @return Pointer to the raw element storage.
        [[nodiscard]] pointer data() noexcept { return m_ptr; }

        /// @return Const pointer to the raw element storage.
        [[nodiscard]] const_pointer data() const noexcept { return m_ptr; }

        // ------------------------------------------------------------------
        // Iterators
        // ------------------------------------------------------------------

        iterator       begin() noexcept { return m_ptr; }        ///< Iterator to first element.
        iterator       end() noexcept { return m_ptr + m_size; } ///< Past-the-end iterator.
        const_iterator begin() const noexcept { return m_ptr; }
        const_iterator end() const noexcept { return m_ptr + m_size; }
        const_iterator cbegin() const noexcept { return m_ptr; }
        const_iterator cend() const noexcept { return m_ptr + m_size; }

        reverse_iterator       rbegin() noexcept { return reverse_iterator(end()); }
        reverse_iterator       rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
        const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

        // ------------------------------------------------------------------
        // Modifiers
        // ------------------------------------------------------------------

        /**
         * @brief Construct an element in place at the end.
         * @tparam Args Constructor argument types.
         * @param args  Arguments forwarded to T's constructor.
         * @return Reference to the newly constructed element.
         */
        template<class... Args>
        reference emplace_back(Args&&... args)
        {
            if (m_size == m_capacity) [[unlikely]]
                grow(m_size + 1);
            std::construct_at(m_ptr + m_size, std::forward<Args>(args)...);
            return m_ptr[m_size++];
        }

        /**
         * @brief Append a copy of @p val.
         * @param val Value to append.
         */
        void push_back(const T& val) { emplace_back(val); }

        /**
         * @brief Append @p val by move.
         * @param val Value to move-append.
         */
        void push_back(T&& val) { emplace_back(std::move(val)); }

        /**
         * @brief Remove the last element.
         *
         * Reverts to inline storage if size drops to ≤ N.
         * Asserts if empty.
         */
        void pop_back() noexcept
        {
            assert(!empty() && "pop_back() on empty small_list");
            std::destroy_at(m_ptr + --m_size);
            try_revert_inline();
        }

        /**
         * @brief Construct an element in place before @p pos.
         *
         * The index of @p pos is saved before any potential reallocation so
         * that the iterator remains valid even if grow() invalidates m_ptr.
         *
         * @tparam Args Constructor argument types.
         * @param pos   Insertion point (element will be placed here).
         * @param args  Arguments forwarded to T's constructor.
         * @return Iterator to the newly inserted element.
         */
        template<class... Args>
        iterator emplace(const_iterator pos, Args&&... args)
        {
            // Save offset before grow() — pos may dangle after reallocation.
            const size_type idx = static_cast<size_type>(pos - m_ptr);
            assert(idx >= 0 && idx <= m_size && "emplace: iterator out of range");

            if (m_size == m_capacity) [[unlikely]]
                grow(m_size + 1);

            if (idx < m_size)
                shift_right(idx); // opens a gap at idx, preserving element order

            std::construct_at(m_ptr + idx, std::forward<Args>(args)...);
            ++m_size;
            return m_ptr + idx;
        }

        /**
         * @brief Insert a copy of @p val before @p pos.
         * @return Iterator to the inserted element.
         */
        iterator insert(const_iterator pos, const T& val) { return emplace(pos, val); }

        /**
         * @brief Insert @p val by move before @p pos.
         * @return Iterator to the inserted element.
         */
        iterator insert(const_iterator pos, T&& val) { return emplace(pos, std::move(val)); }

        /**
         * @brief Erase the element at @p pos.
         *
         * Uses memmove for trivially copyable @p T (single instruction for
         * contiguous geometry data such as vertices and edges).
         * Reverts to inline storage if size drops to ≤ N.
         *
         * @param pos Iterator to the element to remove.
         * @return Iterator to the element that followed @p pos.
         */
        iterator erase(const_iterator pos)
        {
            assert(!empty() && "erase() on empty small_list");
            const size_type idx = static_cast<size_type>(pos - m_ptr);
            assert(idx >= 0 && idx < m_size && "erase: iterator out of range");

            shift_left(idx, 1);
            std::destroy_at(m_ptr + --m_size);
            try_revert_inline();
            return m_ptr + idx;
        }

        /**
         * @brief Erase the range [@p first, @p last).
         *
         * Uses memmove for trivially copyable @p T.
         * Reverts to inline storage if size drops to ≤ N.
         *
         * @param first Iterator to the first element to remove.
         * @param last  Past-the-end iterator of the range to remove.
         * @return Iterator to the element that followed the erased range.
         */
        iterator erase(const_iterator first, const_iterator last)
        {
            assert(first >= m_ptr && last <= m_ptr + m_size && first <= last && "erase: range out of bounds");

            const size_type idx   = static_cast<size_type>(first - m_ptr);
            const size_type count = static_cast<size_type>(last - first);
            if (count == 0)
                return m_ptr + idx;

            shift_left(idx, count);
            destroy_range(m_ptr + m_size - count, m_ptr + m_size);
            m_size -= count;
            try_revert_inline();
            return m_ptr + idx;
        }

        /**
         * @brief Resize the container to @p n elements.
         *
         * If growing: new elements are copy-constructed from @p val.
         * If shrinking: excess elements are destroyed and inline revert is attempted.
         *
         * @param n   Target size.
         * @param val Value used to construct new elements when growing.
         */
        void resize(size_type n, const T& val = T{})
        {
            if (n > m_size)
            {
                reserve(n);
                while (m_size < n)
                    std::construct_at(m_ptr + m_size++, val);
            }
            else
            {
                destroy_range(m_ptr + n, m_ptr + m_size);
                m_size = n;
                try_revert_inline();
            }
        }

        /**
         * @brief Destroy all elements and return to inline storage.
         *
         * Frees any heap buffer. Capacity reverts to N.
         */
        void clear() noexcept
        {
            destroy_range(m_ptr, m_ptr + m_size);
            if (dynamic())
            {
                dealloc(m_ptr);
                m_ptr      = inline_ptr();
                m_capacity = N > 0 ? N : 0;
            }
            m_size = 0;
        }

        /**
         * @brief Swap contents with @p other.
         *
         * O(1) when both containers are dynamic (pointer swap).
         * O(N) when at least one is inline (must relocate elements).
         * The asymmetry is unavoidable — documented here for callers.
         *
         * @param other Container to swap with.
         */
        void swap(small_list& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            if (this == &other)
                return;

            if (dynamic() && other.dynamic())
            {
                // Both on heap: cheap pointer swap, no element touching.
                std::swap(m_ptr, other.m_ptr);
                std::swap(m_size, other.m_size);
                std::swap(m_capacity, other.m_capacity);
                return;
            }

            // At least one is inline: fall back to move-based three-way swap.
            small_list tmp(std::move(*this));
            *this = std::move(other);
            other = std::move(tmp);
        }

        // ------------------------------------------------------------------
        // Search / unique helpers
        // ------------------------------------------------------------------

        /**
         * @brief Linear search for @p val.
         * @param val Value to search for.
         * @return Zero-based index of the first match, or -1 if not found.
         */
        [[nodiscard]] int find_index(const_reference val) const noexcept
        {
            for (size_type i = 0; i < m_size; ++i)
                if (m_ptr[i] == val)
                    return static_cast<int>(i);
            return -1;
        }

        /**
         * @brief Test whether @p val is present.
         * @param val Value to search for.
         * @return true if at least one element compares equal to @p val.
         */
        [[nodiscard]] bool contains(const_reference val) const noexcept
        {
            for (size_type i = 0; i < m_size; ++i)
                if (m_ptr[i] == val)
                    return true;
            return false;
        }

        /**
         * @brief Append @p val only if it is not already present.
         * @param val Value to conditionally append (copy).
         * @return true if the element was inserted; false if it was already present.
         */
        bool insert_unique(const T& val)
        {
            if (contains(val))
                return false;
            push_back(val);
            return true;
        }

        /**
         * @brief Append @p val only if it is not already present (move overload).
         * @param val Value to conditionally move-append.
         * @return true if the element was inserted; false if it was already present.
         */
        bool insert_unique(T&& val)
        {
            if (contains(val))
                return false;
            push_back(std::move(val));
            return true;
        }

        /**
         * @brief Remove the first element equal to @p val.
         * @param val Value to search for and remove.
         * @return true if an element was found and removed; false otherwise.
         */
        bool erase_element(const_reference val)
        {
            for (size_type i = 0; i < m_size; ++i)
            {
                if (m_ptr[i] == val)
                {
                    erase(m_ptr + i);
                    return true;
                }
            }
            return false;
        }
    };

    // ------------------------------------------------------------------
    // Non-member comparison (C++20 three-way)
    // ------------------------------------------------------------------

    /**
     * @brief Equality comparison.
     * @return true if both containers have the same size and equal elements.
     */
    template<class T, sizeType N>
    [[nodiscard]] bool operator==(const small_list<T, N>& lhs, const small_list<T, N>& rhs) noexcept
    {
        if (lhs.size() != rhs.size())
            return false;
        for (sizeType i = 0; i < lhs.size(); ++i)
            if (lhs[i] != rhs[i])
                return false;
        return true;
    }

    /**
     * @brief Lexicographic three-way comparison.
     * @requires T must satisfy std::three_way_comparable.
     */
    template<class T, sizeType N>
        requires std::three_way_comparable<T>
    [[nodiscard]] auto operator<=>(const small_list<T, N>& lhs, const small_list<T, N>& rhs) noexcept
    {
        return std::lexicographical_compare_three_way(
            lhs.begin(),
            lhs.end(),
            rhs.begin(),
            rhs.end());
    }

    // ------------------------------------------------------------------
    // Non-member swap
    // ------------------------------------------------------------------

    /**
     * @brief ADL-friendly swap. Delegates to small_list::swap().
     * @param a First container.
     * @param b Second container.
     */
    template<class T, sizeType N>
    void swap(small_list<T, N>& a, small_list<T, N>& b) noexcept(noexcept(a.swap(b)))
    {
        a.swap(b);
    }

} // namespace un

/// @brief Legacy alias — prefer un::small_list<T, N> in new code.
template<class T, un::sizeType N>
using SmallList = un::small_list<T, N>;
