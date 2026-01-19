// SysMeshUtils.hpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "SysMesh.hpp"

namespace smu // Sys mesh utilities
{

    /**
     * @brief Hash for canonical (sorted) IndexPair edges.
     *      * Assumes the edge is already normalized using SysMesh::sort_edge().
     * Optimized for 32-bit vertex indices.
     *      * This hash is intentionally simple and stable:
     *  - fast (no mixing, no branches)
     *  - suitable for unordered_set / unordered_map
     *  - consistent across platforms
     *      * IMPORTANT:
     *   Always hash sorted edges only.
     *   Use it with SysMesh::sort_edge(edge) before hashing/comparing
     */
    struct IndexPairHash
    {
        size_t operator()(const IndexPair& e) const noexcept
        {
            assert(e.first <= e.second && "IndexPairHash requires sorted edges");

            const uint64_t a = static_cast<uint32_t>(e.first);
            const uint64_t b = static_cast<uint32_t>(e.second);
            return (a << 32) | b;
        }
    };

    /**
     * @brief Ordered interpretation of an unordered edge selection.
     *
     * Selection in SysMesh is unordered by design. Geometry tools, however,
     * require a deterministic traversal order.
     *
     * OrderedEdgePath represents a single connected edge path, either:
     *  - an open chain   (two endpoints, degree-1 vertices)
     *  - a closed loop   (all vertices degree-2)
     *
     * The path is tool-local and should NOT be stored in SysMesh.
     */
    struct OrderedEdgePath
    {
        /// Ordered vertex walk: v0, v1, v2, ...
        std::vector<int32_t> verts;

        /// Ordered edges corresponding to the vertex walk.
        /// Edges are canonical (sorted IndexPair).
        std::vector<IndexPair> edges;

        /// True if the path is a closed loop.
        bool closed = false;

        [[nodiscard]] bool empty() const noexcept
        {
            return edges.empty();
        }
    };

    /**
     * @brief Build ordered edge paths from an unordered edge set.
     *
     * This utility:
     *  - accepts an unordered set of edges (typically selection)
     *  - groups edges into connected components
     *  - produces one or more ordered paths per component that collectively cover all edges
     *
     * Rules:
     *  - Vertices with degree 1 (in the *remaining* subgraph) are treated as endpoints (open chain)
     *  - If no endpoints exist, we treat the walk as a loop (closed cycle) when we return to start
     *  - Vertices with degree > 2 introduce ambiguity; this function resolves it by emitting
     *    multiple paths for the component until all edges are consumed.
     *
     * This function does NOT attempt to detect edge loops or rings in the modeling sense.
     * It only orders connectivity.
     *
     * Typical usage:
     *  - Bevel edges (pass each path, or merge paths per component depending on policy)
     *  - Edge slide / relax along a chain
     *  - Bridge between paths
     *
     * @param edges Unordered edge list. Edges are canonicalized (undirected) internally.
     * @return A list of ordered edge paths. A single connected component may yield multiple paths.
     */
    [[nodiscard]] std::vector<OrderedEdgePath> build_ordered_edge_paths(std::span<const IndexPair> edges);

} // namespace smu

// //
// //  MeshUtil.hpp
// //  Mesh
// //
// //  Created by Daniel Giannopoulos on 6/22/15.
// //  Copyright © 2015 Daniel Giannopoulos. All rights reserved.
// //

// #ifndef SYSMESHUTIL_HPP
// #define SYSMESHUTIL_HPP

// #include <algorithm>

// /// @return A linear interpolation of the two values along 't'.
// /// Example: vec3 edge_center = lerp(edge_pos1, edge_pos2, 0.5f);
// template <class T, class Scalar>
// T lerp(const T& a, const T& b, Scalar t)
// {
//     return a + (b - a) * t;
// }

// /// @return The specified value after clamping it to the range, [low, high].
// template <class T, class T2, class T3>
// T clamp(T val, T2 low, T3 high)
// {
//     assert(low <= high);
//     return std::max(std::min(val, high), low);
// }

// /// Removes the specified element from the container.
// template <class Container>
// void erase_element(Container& c, const typename Container::value_type& element)
// {
//     c.erase(std::find(c.begin(), c.end(), element));
// }

// /// swaps 'a' and 'b' if 'condition' is true.
// template <class T>
// void swap_if(bool condition, T& a, T& b)
// {
//     if (condition)
//         std::swap(a, b);
// }

// /// @return the index to the element in the specified sequence or
// /// -1 if the sequence does not contain the specified element.
// template <class Sequence, class T>
// int find_index(const Sequence& seq, const T& val)
// {
//     for (int j=0, num = static_cast<int>(seq.size()); j < num; ++j)
//     {
//         if (seq[j] == val)
//             return j;
//     }
//     return -1;
// }

// /// Compacts a container, freeing excess memory.
// template <class Container>
// void compact(Container& c)
// {
//     Container(c).swap(c);
// }

// /// Completely clears a container, freeing its memory.
// template <class Container>
// void clear_memory(Container& c)
// {
//     Container().swap(c);
// }

// /// Reverse the contents of the container.
// template<class Container>
// inline Container reverse(Container list)
// {
//     Container res;
//     for (int i = list.size()-1; i >= 0; i--)
//         res.push_back(list[i]);
//     return res;
// }

// #endif // SYSMESHUTIL_HPP
