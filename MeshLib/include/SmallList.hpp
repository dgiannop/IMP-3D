#pragma once

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <span>

namespace un
{
    /// Prefer a signed 32-bit size for your use-case; change if you need >2B elements.
    using sizeType = int32_t;

    /**
     * @brief Small-buffer-optimized vector-like container.
     *
     * Represents an array suitable for inline storage. It uses a fixed capacity
     * by default and stores an array of that size. Only if the array grows beyond
     * this fixed capacity does it trigger a dynamic allocation, at which point
     * it functions very much like std::vector.
     *
     * @tparam T element type
     * @tparam N inline capacity
     */
    template<class T, sizeType N>
    class small_list
    {
    public:
        // standard typedefs
        typedef T                                     value_type;
        typedef T&                                    reference;
        typedef const T&                              const_reference;
        typedef T*                                    pointer;
        typedef const T*                              const_pointer;
        typedef decltype(N)                           size_type;
        typedef T*                                    iterator;
        typedef const T*                              const_iterator;
        typedef std::reverse_iterator<iterator>       reverse_iterator;
        typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

        /// @brief Creates an empty array.
        small_list();

        /// @brief Creates a copy of the array.
        small_list(const small_list& other);

        /// @brief Creates an array of size n with n copies of val.
        explicit small_list(size_type n, value_type val = value_type());

        /// @brief Creates a list from an initializer list.
        small_list(std::initializer_list<T> init);

        /// @brief Destroys the array.
        ~small_list();

        /// @brief Copies the other array to this one.
        small_list& operator=(const small_list& other);

        /// @brief Implicit conversion to std::span<T>.
        constexpr operator std::span<T>() noexcept;

        /// @brief Implicit conversion to std::span<const T>.
        constexpr operator std::span<const T>() const noexcept;

        /// @brief Assigns n copies of val.
        void assign(size_type n, const_reference val);

        /// @brief Assigns elements from range [first, last).
        template<class InputIt>
        void assign(InputIt first, InputIt last);

        /// @brief Assigns elements from an initializer list.
        void assign(std::initializer_list<T> init);

        /// @brief Inserts or erases elements at the end such that the size becomes new_size.
        void resize(size_type n, value_type val = value_type());

        /// @return The index of the first element that matches the one specified or -1
        /// if no match was found.
        [[nodiscard]] int find_index(const_reference val) const;

        /// @return True if the specified element exists in the sequence.
        [[nodiscard]] bool contains(const_reference val) const;

        /// Inserts the specified element to the back of the sequence if it
        /// doesn't already exist.
        /// @return True if the element didn't exist and the insertion was successful.
        bool insert_unique(const_reference val);

        /// Removes the specified element from the list. Note that only the first match is
        /// removed.
        /// @return True if an element was found and removed successfully.
        bool erase_element(const_reference val);

        /// @brief Inserts an element to the end of the array.
        void push_back(const_reference val);

        /// @brief Removes an element from the end of the array.
        void pop_back();

        /// @brief Inserts the specified value before pos.
        void insert(const_iterator pos, const_reference val);

        /// @brief Removes the element at pos.
        void erase(const_iterator pos);

        /// @brief Clears the array of all elements, deallocating memory if
        /// size() > fixed_capacity().
        void clear();

        /// @brief Removes excess capacity.
        void compact();

        /// @return the minimum, fixed memory capacity for the array.
        /// Capacity will always be greater than or equal to this value.
        size_type fixed_capacity() const;

        /// @return the memory capacity for the current array.
        size_type capacity() const;

        /// @brief Reserve capacity for at least new_cap elements (may allocate).
        void reserve(size_type new_cap);

        /// @return true if size() == 0.
        bool empty() const;

        /// @return the number of elements in the array.
        size_type size() const;

        /// @return the first element.
        reference front();

        /// @return the first element.
        const_reference front() const;

        /// @return the last element.
        reference back();

        /// @return the last element.
        const_reference back() const;

        /// @return the nth element.
        reference operator[](size_type n);

        /// @return the nth element.
        const_reference operator[](size_type n) const;

        /// @return a pointer to the contiguous buffer.
        pointer data() const;

        /// @return an iterator pointing to the beginning of the array.
        iterator begin();

        /// @return an iterator pointing to the end of the array.
        iterator end();

        /// @return a read-only iterator pointing to the beginning of the array.
        const_iterator begin() const;

        /// @return a read-only iterator pointing to the end of the array.
        const_iterator end() const;

        /// @return a reverse iterator pointing to the end of the array.
        reverse_iterator rbegin();

        /// @return a reverse iterator pointing to the beginning of the array.
        reverse_iterator rend();

        /// @return a read-only reverse iterator pointing to the end of the array.
        const_reverse_iterator rbegin() const;

        /// @return a read-only reverse iterator pointing to the beginning of the array.
        const_reverse_iterator rend() const;

        /// @brief Swaps the contents of this array with the other.
        void swap(small_list& other);

    private:
        value_type m_fixed[(N > 0 ? N : 1)];
        pointer    m_ptr;
        size_type  m_size;
        size_type  m_capacity;

        bool dynamic() const noexcept
        {
            return m_ptr != m_fixed;
        }
    };

    // ----------------------------------------------------------------------
    // implementation
    // ----------------------------------------------------------------------

    // ---- conversion operators ----

    template<class T, sizeType N>
    constexpr small_list<T, N>::operator std::span<T>() noexcept
    {
        return std::span<T>(m_ptr, static_cast<std::size_t>(m_size));
    }

    template<class T, sizeType N>
    constexpr small_list<T, N>::operator std::span<const T>() const noexcept
    {
        return std::span<const T>(m_ptr, static_cast<std::size_t>(m_size));
    }

    // ---- ctors / dtor / assignment ----

    template<class T, sizeType N>
    small_list<T, N>::small_list() : m_fixed{}, m_ptr(m_fixed), m_size(0), m_capacity(N)
    {
    }

    template<class T, sizeType N>
    small_list<T, N>::small_list(const small_list& other) : m_fixed{}, m_ptr(m_fixed), m_size(0), m_capacity(N)
    {
        assign(other.begin(), other.end());
    }

    template<class T, sizeType N>
    small_list<T, N>::small_list(size_type n, value_type val) : m_fixed{}, m_ptr(m_fixed), m_size(0), m_capacity(N)
    {
        assign(n, val);
    }

    template<class T, sizeType N>
    small_list<T, N>::small_list(std::initializer_list<T> init) : m_fixed{}, m_ptr(m_fixed), m_size(0), m_capacity(N)
    {
        assign(init);
    }

    template<class T, sizeType N>
    small_list<T, N>::~small_list()
    {
        if (dynamic())
        {
            delete[] m_ptr;
        }
        // m_fixed is destroyed automatically
    }

    template<class T, sizeType N>
    small_list<T, N>& small_list<T, N>::operator=(const small_list& other)
    {
        if (this != &other)
        {
            assign(other.begin(), other.end());
        }
        return *this;
    }

    // -------- assign --------

    template<class T, sizeType N>
    void small_list<T, N>::assign(size_type n, const_reference val)
    {
        clear();
        reserve(n);
        for (size_type i = 0; i < n; ++i)
        {
            m_ptr[i] = val;
        }
        m_size = n;
    }

    template<class T, sizeType N>
    template<class InputIt>
    void small_list<T, N>::assign(InputIt first, InputIt last)
    {
        clear();
        for (; first != last; ++first)
            push_back(*first);
    }

    template<class T, sizeType N>
    void small_list<T, N>::assign(std::initializer_list<T> init)
    {
        clear();
        for (const T& v : init)
            push_back(v);
    }

    // -------- capacity helpers --------

    template<class T, sizeType N>
    typename small_list<T, N>::size_type small_list<T, N>::fixed_capacity() const
    {
        return N;
    }

    template<class T, sizeType N>
    typename small_list<T, N>::size_type small_list<T, N>::capacity() const
    {
        return m_capacity;
    }

    template<class T, sizeType N>
    void small_list<T, N>::reserve(size_type new_cap)
    {
        if (new_cap <= m_capacity)
            return;

        if (new_cap <= N && !dynamic())
        {
            // Still fits inline; nothing to do.
            return;
        }

        if (new_cap <= N)
        {
            // If we somehow got dynamic with small cap, just leave it (rare)
            new_cap = N;
        }

        pointer new_buf = new T[static_cast<std::size_t>(new_cap)];

        for (size_type i = 0; i < m_size; ++i)
            new_buf[i] = m_ptr[i];

        if (dynamic())
        {
            delete[] m_ptr;
        }

        m_ptr      = new_buf;
        m_capacity = new_cap;
    }

    template<class T, sizeType N>
    void small_list<T, N>::compact()
    {
        if (!dynamic())
            return;

        if (m_size == m_capacity)
            return;

        pointer new_buf = new T[static_cast<std::size_t>(m_size)];
        for (size_type i = 0; i < m_size; ++i)
            new_buf[i] = m_ptr[i];

        delete[] m_ptr;
        m_ptr      = new_buf;
        m_capacity = m_size;
    }

    // -------- size / state --------

    template<class T, sizeType N>
    bool small_list<T, N>::empty() const
    {
        return m_size == 0;
    }

    template<class T, sizeType N>
    typename small_list<T, N>::size_type small_list<T, N>::size() const
    {
        return m_size;
    }

    // -------- element access --------

    template<class T, sizeType N>
    typename small_list<T, N>::reference small_list<T, N>::front()
    {
        assert(!empty() && "Cannot access the first element of an empty container!");
        return m_ptr[0];
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_reference small_list<T, N>::front() const
    {
        assert(!empty() && "Cannot access the first element of an empty container!");
        return m_ptr[0];
    }

    template<class T, sizeType N>
    typename small_list<T, N>::reference small_list<T, N>::back()
    {
        assert(!empty() && "Cannot access the last element of an empty container!");
        return m_ptr[m_size - 1];
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_reference small_list<T, N>::back() const
    {
        assert(!empty() && "Cannot access the last element of an empty container!");
        return m_ptr[m_size - 1];
    }

    template<class T, sizeType N>
    typename small_list<T, N>::reference small_list<T, N>::operator[](size_type n)
    {
        assert(n >= 0 && n < m_size && "Out of bounds!");
        return m_ptr[n];
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_reference small_list<T, N>::operator[](size_type n) const
    {
        assert(n >= 0 && n < m_size && "Out of bounds!");
        return m_ptr[n];
    }

    template<class T, sizeType N>
    typename small_list<T, N>::pointer small_list<T, N>::data() const
    {
        return m_ptr;
    }

    // -------- iterators --------

    template<class T, sizeType N>
    typename small_list<T, N>::iterator small_list<T, N>::begin()
    {
        return m_ptr;
    }

    template<class T, sizeType N>
    typename small_list<T, N>::iterator small_list<T, N>::end()
    {
        return m_ptr + m_size;
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_iterator small_list<T, N>::begin() const
    {
        return m_ptr;
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_iterator small_list<T, N>::end() const
    {
        return m_ptr + m_size;
    }

    template<class T, sizeType N>
    typename small_list<T, N>::reverse_iterator small_list<T, N>::rbegin()
    {
        return reverse_iterator(end());
    }

    template<class T, sizeType N>
    typename small_list<T, N>::reverse_iterator small_list<T, N>::rend()
    {
        return reverse_iterator(begin());
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_reverse_iterator small_list<T, N>::rbegin() const
    {
        return const_reverse_iterator(end());
    }

    template<class T, sizeType N>
    typename small_list<T, N>::const_reverse_iterator small_list<T, N>::rend() const
    {
        return const_reverse_iterator(begin());
    }

    // -------- modifiers --------

    template<class T, sizeType N>
    void small_list<T, N>::push_back(const_reference val)
    {
        if (!dynamic() && m_size < N)
        {
            m_fixed[m_size++] = val;
            m_ptr             = m_fixed;
            return;
        }

        if (!dynamic())
        {
            // promote to dynamic
            size_type new_cap = (N > 0) ? (N * 2) : 1;
            if (new_cap < m_size + 1)
                new_cap = m_size + 1;

            pointer new_buf = new T[static_cast<std::size_t>(new_cap)];
            for (size_type i = 0; i < m_size; ++i)
                new_buf[i] = m_fixed[i];

            new_buf[m_size++] = val;

            m_ptr      = new_buf;
            m_capacity = new_cap;
            return;
        }

        // already dynamic
        if (m_size == m_capacity)
        {
            size_type new_cap = (m_capacity > 0) ? (m_capacity * 2) : 1;
            if (new_cap < m_size + 1)
                new_cap = m_size + 1;

            pointer new_buf = new T[static_cast<std::size_t>(new_cap)];
            for (size_type i = 0; i < m_size; ++i)
                new_buf[i] = m_ptr[i];

            delete[] m_ptr;
            m_ptr      = new_buf;
            m_capacity = new_cap;
        }

        m_ptr[m_size++] = val;
    }

    template<class T, sizeType N>
    void small_list<T, N>::pop_back()
    {
        assert(m_size > 0 && "pop_back on empty small_list");
        if (!dynamic())
        {
            --m_size;
            return;
        }

        --m_size;

        // if we shrank back to <= N, move back inline
        if (m_size <= N)
        {
            for (size_type i = 0; i < m_size; ++i)
                m_fixed[i] = m_ptr[i];
            delete[] m_ptr;
            m_ptr      = m_fixed;
            m_capacity = N;
        }
    }

    template<class T, sizeType N>
    void small_list<T, N>::resize(size_type n, value_type val)
    {
        if (n > m_size)
        {
            reserve(n);
            while (m_size < n)
                push_back(val);
        }
        else
        {
            while (m_size > n)
                pop_back();
        }
        assert(size() == n);
    }

    template<class T, sizeType N>
    void small_list<T, N>::clear()
    {
        if (dynamic())
        {
            delete[] m_ptr;
            m_ptr      = m_fixed;
            m_capacity = N;
        }
        m_size = 0;
    }

    template<class T, sizeType N>
    void small_list<T, N>::insert(const_iterator pos, const_reference val)
    {
        small_list<T, N> other;

        // insert original elements before insert position
        for (const_iterator it = begin(); it != pos; ++it)
            other.push_back(*it);

        // insert new element
        other.push_back(val);

        // insert original elements at and after insert position
        for (const_iterator end_it = end(); pos != end_it; ++pos)
            other.push_back(*pos);

        *this = other;
    }

    template<class T, sizeType N>
    void small_list<T, N>::erase(const_iterator pos)
    {
        assert(!empty() && "Can't remove elements from an empty container!");
        small_list<T, N> other;

        // insert original elements before the specified position
        for (const_iterator it = begin(); it != pos; ++it)
            other.push_back(*it);

        // insert original elements after the specified position
        const_iterator it = pos;
        ++it;
        for (const_iterator end_it = end(); it != end_it; ++it)
            other.push_back(*it);

        *this = other;
    }

    template<class T, sizeType N>
    void small_list<T, N>::swap(small_list& other)
    {
        small_list<T, N> temp = *this;
        *this                 = other;
        other                 = temp;
    }

    // -------- search / unique helpers --------

    template<class T, sizeType N>
    int small_list<T, N>::find_index(const_reference val) const
    {
        for (size_type i = 0; i < size(); ++i)
        {
            if (m_ptr[i] == val)
                return static_cast<int>(i);
        }
        return -1;
    }

    template<class T, sizeType N>
    bool small_list<T, N>::contains(const_reference val) const
    {
        for (const_iterator it = begin(), end_it = end(); it != end_it; ++it)
        {
            if (*it == val)
                return true;
        }
        return false;
    }

    template<class T, sizeType N>
    bool small_list<T, N>::insert_unique(const_reference val)
    {
        const_pointer end_it = m_ptr + m_size;
        for (const_pointer it = m_ptr; it != end_it; ++it)
        {
            if (*it == val)
                return false;
        }
        push_back(val);
        return true;
    }

    template<class T, sizeType N>
    bool small_list<T, N>::erase_element(const_reference val)
    {
        for (iterator it = begin(), end_it = end(); it != end_it; ++it)
        {
            if (*it == val)
            {
                erase(it);
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------
    // equality
    // ------------------------------------------------------------------

    template<class T, std::int32_t N>
    constexpr bool operator==(const small_list<T, N>& lhs, const small_list<T, N>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        auto other_it = rhs.begin();
        for (auto it = lhs.begin(), end = lhs.end(); it != end; ++it, ++other_it)
        {
            if (*it != *other_it)
            {
                return false;
            }
        }
        return true;
    }

    template<class T, sizeType N>
    bool operator!=(const small_list<T, N>& lhs, const small_list<T, N>& rhs)
    {
        return !(lhs == rhs);
    }

} // namespace un

// legacy alias for old code:
template<class T, un::sizeType N>
using SmallList = un::small_list<T, N>;
