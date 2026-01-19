#include "SysMeshUtils.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace smu
{

    namespace
    {
        using AdjMap = std::unordered_map<int32_t, std::vector<int32_t>>;

        static IndexPair canon_edge(int32_t a, int32_t b) noexcept
        {
            return SysMesh::sort_edge({a, b});
        }

        static uint64_t pack_undirected_i32(int32_t a, int32_t b) noexcept
        {
            if (a > b)
                std::swap(a, b);
            return (uint64_t(uint32_t(a)) << 32) | uint64_t(uint32_t(b));
        }

        static void add_adj(AdjMap& adj, int32_t a, int32_t b)
        {
            adj[a].push_back(b);
            adj[b].push_back(a);
        }

        static void dedup(std::vector<int32_t>& v)
        {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        }

        static int32_t find_endpoint_or_min_remaining(
            const std::vector<int32_t>&         compVerts,
            const AdjMap&                       adj,
            const std::unordered_set<uint64_t>& remaining) noexcept
        {
            int32_t bestEndpoint = std::numeric_limits<int32_t>::max();

            auto degree_remaining = [&](int32_t v) noexcept -> int {
                auto it = adj.find(v);
                if (it == adj.end())
                    return 0;

                int deg = 0;
                for (int32_t n : it->second)
                {
                    if (remaining.contains(pack_undirected_i32(v, n)))
                        ++deg;
                }
                return deg;
            };

            // Prefer smallest degree-1 vertex (open chain endpoint) in the *remaining* subgraph.
            for (int32_t v : compVerts)
            {
                if (degree_remaining(v) == 1)
                    bestEndpoint = std::min(bestEndpoint, v);
            }

            if (bestEndpoint != std::numeric_limits<int32_t>::max())
                return bestEndpoint;

            // Otherwise pick smallest vertex that still has any remaining incident edge.
            int32_t best = std::numeric_limits<int32_t>::max();
            for (int32_t v : compVerts)
            {
                if (degree_remaining(v) > 0)
                    best = std::min(best, v);
            }

            return (best != std::numeric_limits<int32_t>::max()) ? best : -1;
        }

        static bool component_has_remaining_edges(
            const std::vector<int32_t>&         compVerts,
            const AdjMap&                       adj,
            const std::unordered_set<uint64_t>& remaining) noexcept
        {
            for (int32_t v : compVerts)
            {
                auto it = adj.find(v);
                if (it == adj.end())
                    continue;

                for (int32_t n : it->second)
                {
                    if (remaining.contains(pack_undirected_i32(v, n)))
                        return true;
                }
            }
            return false;
        }

    } // namespace

    std::vector<OrderedEdgePath> build_ordered_edge_paths(std::span<const IndexPair> edges)
    {
        std::vector<OrderedEdgePath> result;
        if (edges.empty())
            return result;

        // ---------------------------------------------------------------------
        // 1) Canonicalize edges, build adjacency, and build a "remaining" edge set.
        //    The remaining set is the source of truth for what has not yet been
        //    assigned to an output path.
        // ---------------------------------------------------------------------
        AdjMap adj;
        adj.reserve(edges.size() * 2u);

        std::unordered_set<uint64_t> remaining;
        remaining.reserve(edges.size() * 2u);

        for (const IndexPair& e0 : edges)
        {
            if (e0.first < 0 || e0.second < 0)
                continue;
            if (e0.first == e0.second)
                continue;

            const IndexPair e = SysMesh::sort_edge(e0);

            add_adj(adj, e.first, e.second);
            remaining.insert(pack_undirected_i32(e.first, e.second));
        }

        if (adj.empty() || remaining.empty())
            return result;

        for (auto& [_, nbrs] : adj)
            dedup(nbrs);

        // ---------------------------------------------------------------------
        // 2) Walk connected components (by vertices).
        // ---------------------------------------------------------------------
        std::unordered_set<int32_t> visitedVerts;
        visitedVerts.reserve(adj.size() * 2u);

        for (const auto& [seedVert, _] : adj)
        {
            if (visitedVerts.contains(seedVert))
                continue;

            // DFS to collect all vertices in this component.
            std::vector<int32_t> stack;
            stack.push_back(seedVert);

            std::vector<int32_t> compVerts;
            compVerts.reserve(64);

            visitedVerts.insert(seedVert);

            while (!stack.empty())
            {
                const int32_t v = stack.back();
                stack.pop_back();
                compVerts.push_back(v);

                auto it = adj.find(v);
                if (it == adj.end())
                    continue;

                for (int32_t n : it->second)
                {
                    if (!visitedVerts.contains(n))
                    {
                        visitedVerts.insert(n);
                        stack.push_back(n);
                    }
                }
            }

            if (compVerts.empty())
                continue;

            // -----------------------------------------------------------------
            // 3) Emit one or more paths for this component until all its edges
            //    have been consumed from the global "remaining" set.
            // -----------------------------------------------------------------
            while (component_has_remaining_edges(compVerts, adj, remaining))
            {
                const int32_t start = find_endpoint_or_min_remaining(compVerts, adj, remaining);
                if (start < 0)
                    break;

                OrderedEdgePath path = {};
                path.closed          = false;
                path.verts.clear();
                path.edges.clear();

                path.verts.push_back(start);

                int32_t prev = -1;
                int32_t cur  = start;

                // Linear walk: at branches, choose a stable next (smallest vertex id).
                for (;;)
                {
                    auto it = adj.find(cur);
                    if (it == adj.end())
                        break;

                    int32_t next = -1;

                    for (int32_t n : it->second)
                    {
                        if (n == prev)
                            continue;

                        const uint64_t k = pack_undirected_i32(cur, n);
                        if (!remaining.contains(k))
                            continue;

                        if (next < 0 || n < next)
                            next = n;
                    }

                    // Endpoint or dead end in the remaining subgraph.
                    if (next < 0)
                        break;

                    // Consume edge.
                    remaining.erase(pack_undirected_i32(cur, next));

                    const IndexPair e = canon_edge(cur, next);
                    path.edges.push_back(e);
                    path.verts.push_back(next);

                    prev = cur;
                    cur  = next;

                    // Closed cycle detected (we returned to start after consuming at least one edge).
                    if (cur == start)
                    {
                        path.closed = true;
                        break;
                    }
                }

                if (!path.edges.empty())
                    result.push_back(std::move(path));
                else
                    break; // safety: avoid infinite loop if something went wrong
            }
        }

        return result;
    }

} // namespace smu
