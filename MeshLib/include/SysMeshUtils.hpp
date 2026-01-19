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
