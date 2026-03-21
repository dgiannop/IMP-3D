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
    using sizeType = int32_t;

    /**
     * @brief Small-buffer-optimized vector-like container (C++23).
     *
     * Stores up to N elements inline with no heap allocation. Spills to the
     * heap only when the inline capacity is exceeded. All element lifetimes
     * are managed via placement-new / explicit destructor calls, so T need
     * not be default-constructible and no unnecessary constructions occur.
     *
     * @tparam T  Element type. Must be move-constructible for most operations.
     * @tparam N  Inline (fixed) capacity.
     */
    template<class T, sizeType N>
    class small_list
    {
        // ------------------------------------------------------------------
        // internal storage
        // ------------------------------------------------------------------

        // NOTE: Declaration order matters — m_fixed must be declared before
        // m_ptr so that inline_ptr() is valid when m_ptr is initialized in
        // the member initializer list (members init in declaration order).

        /// Raw inline buffer — properly aligned, never default-constructed.
        alignas(T) std::byte m_fixed[sizeof(T) * (N > 0 ? N : 1)];

        T*       m_ptr; ///< Points to m_fixed (inline) or heap buffer.
        sizeType m_size;
        sizeType m_capacity;

        // ------------------------------------------------------------------
        // private helpers
        // ------------------------------------------------------------------

        [[nodiscard]] bool dynamic() const noexcept { return m_ptr != inline_ptr(); }

        [[nodiscard]] T* inline_ptr() noexcept
        {
            return std::launder(reinterpret_cast<T*>(m_fixed));
        }

        [[nodiscard]] const T* inline_ptr() const noexcept
        {
            return std::launder(reinterpret_cast<const T*>(m_fixed));
        }

        /// Allocate a raw (uninitialized) buffer of `cap` elements.
        [[nodiscard]] static T* alloc(sizeType cap)
        {
            return static_cast<T*>(::operator new(static_cast<std::size_t>(cap) * sizeof(T)));
        }

        /// Free a raw buffer previously returned by alloc().
        static void dealloc(T* p) noexcept { ::operator delete(p); }

        /// Destroy elements in [first, last). No-op for trivially destructible T.
        static void destroy_range(T* first, T* last) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
                for (; first != last; ++first)
                    std::destroy_at(first);
        }

        /// Relocate n elements from src into uninitialized dst.
        /// For trivially copyable T compiles to a single memcpy.
        /// For non-trivial T: move-constructs into dst then destroys src range.
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

        /// Attempt to return to inline storage. Called after any size reduction.
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

        /// Grow to at least `needed` capacity using 1.5x growth, relocating existing elements.
        void grow(sizeType needed)
        {
            sizeType new_cap = (m_capacity > 0) ? (m_capacity + m_capacity / 2) : 1;
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
        // standard typedefs
        // ------------------------------------------------------------------
        using value_type             = T;
        using reference              = T&;
        using const_reference        = const T&;
        using pointer                = T*;
        using const_pointer          = const T*;
        using size_type              = sizeType;
        using difference_type        = std::ptrdiff_t;
        using iterator               = T*;
        using const_iterator         = const T*;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // ------------------------------------------------------------------
        // construction / destruction
        // ------------------------------------------------------------------

        /// Default-constructs an empty list.
        small_list() noexcept
            : m_ptr(inline_ptr()),
              m_size(0),
              m_capacity(N > 0 ? N : 0)
        {
        }

        /// Copy constructor.
        small_list(const small_list& other) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            reserve(other.m_size);
            for (sizeType i = 0; i < other.m_size; ++i)
                std::construct_at(m_ptr + i, other.m_ptr[i]);
            m_size = other.m_size;
        }

        /// Move constructor — O(1) when other is dynamic, O(N) when inline.
        small_list(small_list&& other) noexcept(std::is_nothrow_move_constructible_v<T>) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            if (other.dynamic())
            {
                // Steal the heap buffer.
                m_ptr      = other.m_ptr;
                m_size     = other.m_size;
                m_capacity = other.m_capacity;

                other.m_ptr      = other.inline_ptr();
                other.m_size     = 0;
                other.m_capacity = N > 0 ? N : 0;
            }
            else
            {
                // Inline storage can't be "moved" — must relocate elements.
                relocate(m_ptr, other.m_ptr, other.m_size);
                m_size       = other.m_size;
                other.m_size = 0;
            }
        }

        /// Fill constructor: n copies of val.
        explicit small_list(size_type n, const T& val = T{}) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            assign(n, val);
        }

        /// Range constructor.
        template<std::input_iterator It>
        small_list(It first, It last) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            assign(first, last);
        }

        /// Initializer-list constructor.
        small_list(std::initializer_list<T> init) : m_ptr(inline_ptr()), m_size(0), m_capacity(N > 0 ? N : 0)
        {
            assign(init);
        }

        /// Destructor.
        ~small_list()
        {
            destroy_range(m_ptr, m_ptr + m_size);
            if (dynamic())
                dealloc(m_ptr);
        }

        // ------------------------------------------------------------------
        // assignment
        // ------------------------------------------------------------------

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

        small_list& operator=(std::initializer_list<T> init)
        {
            assign(init);
            return *this;
        }

        // ------------------------------------------------------------------
        // span conversion
        // ------------------------------------------------------------------

        constexpr operator std::span<T>() noexcept
        {
            return {m_ptr, static_cast<std::size_t>(m_size)};
        }

        constexpr operator std::span<const T>() const noexcept
        {
            return {m_ptr, static_cast<std::size_t>(m_size)};
        }

        // ------------------------------------------------------------------
        // assign
        // ------------------------------------------------------------------

        void assign(size_type n, const T& val)
        {
            clear();
            reserve(n);
            for (size_type i = 0; i < n; ++i)
                std::construct_at(m_ptr + i, val);
            m_size = n;
        }

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

        void assign(std::initializer_list<T> init)
        {
            assign(init.begin(), init.end());
        }

        // ------------------------------------------------------------------
        // capacity
        // ------------------------------------------------------------------

        [[nodiscard]] size_type fixed_capacity() const noexcept { return N; }
        [[nodiscard]] size_type capacity() const noexcept { return m_capacity; }
        [[nodiscard]] bool      empty() const noexcept { return m_size == 0; }
        [[nodiscard]] size_type size() const noexcept { return m_size; }

        void reserve(size_type new_cap)
        {
            if (new_cap <= m_capacity)
                return;
            grow(new_cap);
        }

        /// Shrink capacity to size(). Moves back to inline buffer if size() <= N.
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
        // element access
        // ------------------------------------------------------------------

        [[nodiscard]] reference front()
        {
            assert(!empty() && "front() on empty small_list");
            return m_ptr[0];
        }
        [[nodiscard]] const_reference front() const
        {
            assert(!empty() && "front() on empty small_list");
            return m_ptr[0];
        }

        [[nodiscard]] reference back()
        {
            assert(!empty() && "back() on empty small_list");
            return m_ptr[m_size - 1];
        }
        [[nodiscard]] const_reference back() const
        {
            assert(!empty() && "back() on empty small_list");
            return m_ptr[m_size - 1];
        }

        [[nodiscard]] reference operator[](size_type n)
        {
            assert(n >= 0 && n < m_size && "small_list: index out of bounds");
            return m_ptr[n];
        }
        [[nodiscard]] const_reference operator[](size_type n) const
        {
            assert(n >= 0 && n < m_size && "small_list: index out of bounds");
            return m_ptr[n];
        }

        [[nodiscard]] pointer       data() noexcept { return m_ptr; }
        [[nodiscard]] const_pointer data() const noexcept { return m_ptr; }

        // ------------------------------------------------------------------
        // iterators
        // ------------------------------------------------------------------

        iterator       begin() noexcept { return m_ptr; }
        iterator       end() noexcept { return m_ptr + m_size; }
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
        // modifiers
        // ------------------------------------------------------------------

        /// Construct element in place at the end.
        template<class... Args>
        reference emplace_back(Args&&... args)
        {
            if (m_size == m_capacity) [[unlikely]]
                grow(m_size + 1);
            std::construct_at(m_ptr + m_size, std::forward<Args>(args)...);
            return m_ptr[m_size++];
        }

        void push_back(const T& val) { emplace_back(val); }
        void push_back(T&& val) { emplace_back(std::move(val)); }

        void pop_back() noexcept
        {
            assert(!empty() && "pop_back() on empty small_list");
            std::destroy_at(m_ptr + --m_size);
            try_revert_inline();
        }

        /// Construct element in place before pos. Returns iterator to new element.
        /// NOTE: idx is saved before grow() since grow() may reallocate m_ptr,
        /// invalidating pos. idx remains valid because it is an offset, not a pointer.
        template<class... Args>
        iterator emplace(const_iterator pos, Args&&... args)
        {
            const size_type idx = static_cast<size_type>(pos - m_ptr);
            assert(idx >= 0 && idx <= m_size && "emplace: iterator out of range");

            if (m_size == m_capacity) [[unlikely]]
                grow(m_size + 1); // pos is now dangling — use idx only from here

            if (idx < m_size)
            {
                std::construct_at(m_ptr + m_size, std::move(m_ptr[m_size - 1]));
                for (sizeType i = m_size - 1; i > idx; --i)
                    m_ptr[i] = std::move(m_ptr[i - 1]);
                std::destroy_at(m_ptr + idx);
            }
            std::construct_at(m_ptr + idx, std::forward<Args>(args)...);
            ++m_size;
            return m_ptr + idx;
        }

        iterator insert(const_iterator pos, const T& val) { return emplace(pos, val); }
        iterator insert(const_iterator pos, T&& val) { return emplace(pos, std::move(val)); }

        /// Erase element at pos. Returns iterator to next element.
        iterator erase(const_iterator pos)
        {
            assert(!empty() && "erase() on empty small_list");
            const size_type idx = static_cast<size_type>(pos - m_ptr);
            assert(idx >= 0 && idx < m_size && "erase: iterator out of range");

            for (size_type i = idx; i < m_size - 1; ++i)
                m_ptr[i] = std::move(m_ptr[i + 1]);
            std::destroy_at(m_ptr + --m_size);

            return m_ptr + idx;
        }

        /// Erase range [first, last). Returns iterator to next element.
        iterator erase(const_iterator first, const_iterator last)
        {
            assert(first >= m_ptr && last <= m_ptr + m_size && first <= last && "erase: range out of bounds");

            const size_type idx   = static_cast<size_type>(first - m_ptr);
            const size_type count = static_cast<size_type>(last - first);
            if (count == 0)
                return m_ptr + idx;

            for (size_type i = idx; i + count < m_size; ++i)
                m_ptr[i] = std::move(m_ptr[i + count]);

            destroy_range(m_ptr + m_size - count, m_ptr + m_size);
            m_size -= count;
            return m_ptr + idx;
        }

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
                // Return to inline storage if we've shrunk enough to fit.
                try_revert_inline();
            }
        }

        /// Clears all elements. Returns to inline storage if currently dynamic.
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

        void swap(small_list& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            if (this == &other)
                return;

            if (dynamic() && other.dynamic())
            {
                // Both dynamic: just swap pointers and scalars, no element touching.
                std::swap(m_ptr, other.m_ptr);
                std::swap(m_size, other.m_size);
                std::swap(m_capacity, other.m_capacity);
                return;
            }

            // At least one is inline: use move semantics.
            small_list tmp(std::move(*this));
            *this = std::move(other);
            other = std::move(tmp);
        }

        // ------------------------------------------------------------------
        // search / unique helpers
        // ------------------------------------------------------------------

        [[nodiscard]] int find_index(const_reference val) const noexcept
        {
            for (size_type i = 0; i < m_size; ++i)
                if (m_ptr[i] == val)
                    return static_cast<int>(i);
            return -1;
        }

        [[nodiscard]] bool contains(const_reference val) const noexcept
        {
            for (size_type i = 0; i < m_size; ++i)
                if (m_ptr[i] == val)
                    return true;
            return false;
        }

        /// Appends val only if it is not already present.
        /// @return true if the element was inserted.
        bool insert_unique(const T& val)
        {
            if (contains(val))
                return false;
            push_back(val);
            return true;
        }

        bool insert_unique(T&& val)
        {
            if (contains(val))
                return false;
            push_back(std::move(val));
            return true;
        }

        /// Removes the first element equal to val.
        /// @return true if an element was found and removed.
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
    // non-member comparison (C++20 three-way)
    // ------------------------------------------------------------------

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
    // non-member swap
    // ------------------------------------------------------------------

    template<class T, sizeType N>
    void swap(small_list<T, N>& a, small_list<T, N>& b) noexcept(noexcept(a.swap(b)))
    {
        a.swap(b);
    }

} // namespace un

// Legacy alias.
template<class T, un::sizeType N>
using SmallList = un::small_list<T, N>;
