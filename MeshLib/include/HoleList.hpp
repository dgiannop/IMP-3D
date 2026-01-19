#ifndef HOLE_LIST_HPP_INCLUDED
#define HOLE_LIST_HPP_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <vector>

template<typename T>
class HoleList
{
public:
    using value_type             = T;
    using reference              = T&;
    using const_reference        = const T&;
    using pointer                = T*;
    using const_pointer          = const T*;
    using size_type              = std::int32_t;
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

    [[nodiscard]] size_type size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] size_type capacity() const noexcept // TODO: This is misslieading
    {
        return static_cast<size_type>(m_elements.size());
    }

    [[nodiscard]] iterator begin() noexcept
    {
        return m_elements.begin();
    }

    [[nodiscard]] iterator end() noexcept
    {
        return m_elements.end();
    }

    [[nodiscard]] const_iterator begin() const noexcept
    {
        return m_elements.begin();
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return m_elements.end();
    }

    [[nodiscard]] reference operator[](int32_t index) noexcept
    {
        return m_elements[index];
    }

    [[nodiscard]] const_reference operator[](int32_t index) const noexcept
    {
        return m_elements[index];
    }

    // Copy-insert: only enabled if T is copy-constructible
    template<typename Q = T>
    std::enable_if_t<std::is_copy_constructible_v<Q>, uint32_t>
    insert(const T& element)
    {
        return insert_impl(element);
    }

    // Move-insert: always enabled
    uint32_t insert(T&& element)
    {
        return insert_impl(std::move(element));
    }

    void remove(int32_t index)
    {
        assert(index >= 0 && static_cast<size_type>(index) < static_cast<size_type>(m_elements.size()));

        // todo: slow. Just to check for now:
        // Catch double-remove
        assert(std::find(m_freeIndices.begin(), m_freeIndices.end(), index) == m_freeIndices.end());

        --m_size;
        m_freeIndices.push_back(index);
        m_dirty = true;
    }

    void clear() noexcept
    {
        m_elements.clear();
        m_freeIndices.clear();
        m_cachedValidIndices.clear();
        m_dirty = true;
        m_size  = 0;
    }

    void reserve(size_type amount)
    {
        m_elements.reserve(amount);
    }

    void swap(HoleList& other) noexcept
    {
        using std::swap;
        swap(m_elements, other.m_elements);
        swap(m_freeIndices, other.m_freeIndices);
        swap(m_cachedValidIndices, other.m_cachedValidIndices);
        swap(m_size, other.m_size);
        swap(m_dirty, other.m_dirty);
    }

    [[nodiscard]] const std::vector<int32_t>& valid_indices() const
    {
        if (!m_dirty)
            return m_cachedValidIndices;

        std::vector<bool> occupied(m_elements.size(), true);
        for (int32_t idx : m_freeIndices)
            occupied[idx] = false;

        m_cachedValidIndices.clear();
        m_cachedValidIndices.reserve(m_size);
        for (int32_t i = 0; i < static_cast<int32_t>(m_elements.size()); ++i)
        {
            if (occupied[i])
                m_cachedValidIndices.push_back(i);
        }

        m_dirty = false;
        return m_cachedValidIndices;
    }

private:
    template<typename U>
    uint32_t insert_impl(U&& element)
    {
        uint32_t index;
        if (m_freeIndices.empty())
        {
            index = static_cast<uint32_t>(m_elements.size());
            m_elements.push_back(std::forward<U>(element));
        }
        else
        {
            index = m_freeIndices.back();
            m_freeIndices.pop_back();
            m_elements[index] = std::forward<U>(element);
        }
        ++m_size;
        m_dirty = true;
        return index;
    }

    std::vector<T>               m_elements;
    std::vector<int32_t>         m_freeIndices;
    mutable std::vector<int32_t> m_cachedValidIndices;
    mutable bool                 m_dirty = true;
    size_type                    m_size  = 0;
};

#endif // HOLE_LIST_HPP_INCLUDED
