#ifndef EDGE_SET_HPP_INCLUDED
#define EDGE_SET_HPP_INCLUDED

#include <algorithm> // For std::swap
#include <cstdint>
#include <set>

// Todo: notes. If I'm going to use <set> then m_size is not needed
// I can get the size of edges.size()

using IndexPair = std::pair<int32_t, int32_t>;

class EdgeSet
{
public:
    // Creates an empty edge set.
    EdgeSet() noexcept : m_size{0}
    {
    }

    // @return size() == 0.
    [[nodiscard]] bool empty() const noexcept
    {
        return m_size == 0;
    }

    // @return The number of edges in the container.
    [[nodiscard]] size_t size() const noexcept
    {
        return m_size;
    }

    // Removes all edges from the container.
    void clear() noexcept
    {
        edges.clear();
    }

    // @return True if the set contains the specified edge.
    [[nodiscard]] bool contains(IndexPair edge) const noexcept
    {
        normalize(edge);
        return edges.contains(edge);
    }

    // Inserts the specified edge if it doesn't already exist.
    // @return True if the insertion was successful.
    bool insert(IndexPair edge) noexcept
    {
        normalize(edge);
        if (edges.insert(edge).second)
        {
            ++m_size;
            return true;
        }
        return false;
    }

    // Removes the specified edge if it exists in the set.
    // @return True if the removal was successful.
    bool erase(IndexPair edge) noexcept
    {
        normalize(edge);
        if (edges.erase(edge) != 0)
        {
            --m_size;
            return true;
        }
        return false;
    }

    // Swaps the contents of this set with the other.
    void swap(EdgeSet& other) noexcept
    {
        std::swap(edges, other.edges);
        std::swap(m_size, other.m_size);
    }

    auto begin() noexcept
    {
        return edges.begin();
    }

    auto end() noexcept
    {
        return edges.end();
    }

    // Normalize the edge to ensure the first element is always smaller than the second.
    static void normalize(IndexPair& edge) noexcept
    {
        if (edge.first > edge.second)
        {
            std::swap(edge.first, edge.second);
        }
    }

private:
    std::set<IndexPair> edges;
    uint32_t            m_size; // Why not get the size of edges.size()
};

// @return True if the two edges have the same pair of vertices. Edge order doesn't matter.
inline bool same_edge(IndexPair edge1, IndexPair edge2) noexcept
{
    EdgeSet::normalize(edge1);
    EdgeSet::normalize(edge2);
    return edge1 == edge2;
}

#endif // EDGE_SET_HPP_INCLUDED
