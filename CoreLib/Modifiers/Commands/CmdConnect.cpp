//=============================================================================
// CmdConnect.cpp
//=============================================================================
#include "CmdConnect.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scene.hpp"
#include "SysMesh.hpp"

namespace
{
    // ------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------
    struct EdgeHash
    {
        size_t operator()(const IndexPair& e) const noexcept
        {
            const uint64_t a = static_cast<uint32_t>(e.first);
            const uint64_t b = static_cast<uint32_t>(e.second);
            return static_cast<size_t>((a << 32) ^ b);
        }
    };

    static std::vector<int32_t> collect_maps_to_preserve(const SysMesh* mesh)
    {
        std::vector<int32_t> maps = {};
        maps.reserve(8);

        constexpr int32_t kProbeMin = 0;
        constexpr int32_t kProbeMax = 15;

        for (int32_t id = kProbeMin; id <= kProbeMax; ++id)
        {
            const int32_t map = mesh->map_find(id);
            if (map >= 0)
                maps.push_back(map);
        }

        return maps;
    }

    static bool poly_contains_edge(const SysPolyVerts& pv, const IndexPair& eSorted) noexcept
    {
        const int n = static_cast<int>(pv.size());
        if (n < 2)
            return false;

        for (int i = 0; i < n; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) % n];

            if (SysMesh::sort_edge({a, b}) == eSorted)
                return true;
        }

        return false;
    }

    static int find_edge_index_in_poly(const SysPolyVerts& pv, const IndexPair& eSorted) noexcept
    {
        const int n = static_cast<int>(pv.size());
        for (int i = 0; i < n; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) % n];
            if (SysMesh::sort_edge({a, b}) == eSorted)
                return i;
        }
        return -1;
    }

    static IndexPair poly_edge_at(const SysPolyVerts& pv, int i) noexcept
    {
        const int     n = static_cast<int>(pv.size());
        const int32_t a = pv[i];
        const int32_t b = pv[(i + 1) % n];
        return SysMesh::sort_edge({a, b});
    }

    // ------------------------------------------------------------
    // Split a specific edge within a specific polygon (local split).
    //
    // Returns: {newPolyId, newVertexId}
    //
    // This avoids modifying every poly sharing the edge and keeps the operation local.
    // ------------------------------------------------------------
    static std::pair<int32_t, int32_t> split_edge_in_poly(SysMesh&                    mesh,
                                                          int32_t                     poly,
                                                          const IndexPair&            edgeIn,
                                                          float                       t,
                                                          const std::vector<int32_t>& maps)
    {
        if (!mesh.poly_valid(poly))
            return {-1, -1};

        const IndexPair edge = SysMesh::sort_edge(edgeIn);

        const SysPolyVerts pv = mesh.poly_verts(poly);
        const int          n  = static_cast<int>(pv.size());
        if (n < 3)
            return {-1, -1};

        if (!poly_contains_edge(pv, edge))
            return {-1, -1};

        for (int32_t v : pv)
        {
            if (!mesh.vert_valid(v))
                return {-1, -1};
        }

        // Create the new vertex at t on the geometric edge.
        const int32_t a = edge.first;
        const int32_t b = edge.second;

        const glm::vec3 pa = mesh.vert_position(a);
        const glm::vec3 pb = mesh.vert_position(b);
        const glm::vec3 pm = glm::mix(pa, pb, t);

        const int32_t vm = mesh.create_vert(pm);

        // Gather per-map corner arrays (only if aligned 1:1 with pv)
        struct PolyMapInfo
        {
            int32_t      map   = -1;
            SysPolyVerts mpv   = {};
            bool         valid = false;
        };

        std::vector<PolyMapInfo> polyMaps = {};
        polyMaps.reserve(maps.size());

        for (int32_t map : maps)
        {
            PolyMapInfo info = {};
            info.map         = map;

            if (mesh.map_poly_valid(map, poly))
            {
                info.mpv   = mesh.map_poly_verts(map, poly);
                info.valid = (info.mpv.size() == pv.size());
            }

            polyMaps.push_back(info);
        }

        SysPolyVerts nv = {};
        nv.reserve(pv.size() + 1);

        std::vector<SysPolyVerts> newMapPolys = {};
        newMapPolys.resize(polyMaps.size());

        for (size_t mi = 0; mi < polyMaps.size(); ++mi)
        {
            if (polyMaps[mi].valid)
                newMapPolys[mi].reserve(polyMaps[mi].mpv.size() + 1);
        }

        for (int i = 0; i < n; ++i)
        {
            const int32_t v0 = pv[i];
            const int32_t v1 = pv[(i + 1) % n];

            nv.push_back(v0);
            for (size_t mi = 0; mi < polyMaps.size(); ++mi)
            {
                if (polyMaps[mi].valid)
                    newMapPolys[mi].push_back(polyMaps[mi].mpv[i]);
            }

            if (SysMesh::sort_edge({v0, v1}) == edge)
            {
                nv.push_back(vm);

                for (size_t mi = 0; mi < polyMaps.size(); ++mi)
                {
                    if (!polyMaps[mi].valid)
                        continue;

                    const int32_t mv0 = polyMaps[mi].mpv[i];
                    const int32_t mv1 = polyMaps[mi].mpv[(i + 1) % n];

                    const float* a0 = mesh.map_vert_position(polyMaps[mi].map, mv0);
                    const float* a1 = mesh.map_vert_position(polyMaps[mi].map, mv1);

                    const int dim = mesh.map_dim(polyMaps[mi].map);

                    float tmp[4] = {0.f, 0.f, 0.f, 0.f};
                    for (int k = 0; k < dim && k < 4; ++k)
                        tmp[k] = a0[k] + (a1[k] - a0[k]) * t;

                    const int32_t mvm = mesh.map_create_vert(polyMaps[mi].map, tmp);
                    newMapPolys[mi].push_back(mvm);
                }
            }
        }

        const int32_t newPoly = mesh.clone_poly(poly, nv);

        for (size_t mi = 0; mi < polyMaps.size(); ++mi)
        {
            if (!polyMaps[mi].valid)
                continue;

            mesh.map_create_poly(polyMaps[mi].map, newPoly, newMapPolys[mi]);
            mesh.map_remove_poly(polyMaps[mi].map, poly);
        }

        mesh.remove_poly(poly);
        return {newPoly, vm};
    }

    // ------------------------------------------------------------
    // Split polygon by connecting two boundary vertices (vA, vB)
    // Replaces poly with two new polys. Preserves probed maps when 1:1.
    // Returns: {p1, p2} or {-1,-1} on failure.
    // ------------------------------------------------------------
    static std::pair<int32_t, int32_t> split_poly_connect_boundary(SysMesh&                    mesh,
                                                                   int32_t                     poly,
                                                                   int32_t                     vA,
                                                                   int32_t                     vB,
                                                                   const std::vector<int32_t>& maps)
    {
        if (!mesh.poly_valid(poly))
            return {-1, -1};

        const SysPolyVerts pv = mesh.poly_verts(poly);
        const int          n  = static_cast<int>(pv.size());
        if (n < 4)
            return {-1, -1};

        int ia = -1;
        int ib = -1;
        for (int i = 0; i < n; ++i)
        {
            if (pv[i] == vA)
                ia = i;
            if (pv[i] == vB)
                ib = i;
        }
        if (ia < 0 || ib < 0 || ia == ib)
            return {-1, -1};

        SysPolyVerts path1 = {};
        for (int i = ia; i != ib; i = (i + 1) % n)
            path1.push_back(pv[i]);
        path1.push_back(pv[ib]);

        SysPolyVerts path2 = {};
        for (int i = ib; i != ia; i = (i + 1) % n)
            path2.push_back(pv[i]);
        path2.push_back(pv[ia]);

        if (path1.size() < 3 || path2.size() < 3)
            return {-1, -1};

        struct PolyMapInfo
        {
            int32_t      map   = -1;
            SysPolyVerts mpv   = {};
            bool         valid = false;
        };

        std::vector<PolyMapInfo> polyMaps = {};
        polyMaps.reserve(maps.size());

        for (int32_t map : maps)
        {
            PolyMapInfo info = {};
            info.map         = map;

            if (mesh.map_poly_valid(map, poly))
            {
                info.mpv   = mesh.map_poly_verts(map, poly);
                info.valid = (info.mpv.size() == pv.size());
            }

            polyMaps.push_back(info);
        }

        auto buildMapped = [&](const SysPolyVerts& path, const SysPolyVerts& mpv) -> SysPolyVerts {
            SysPolyVerts out = {};
            out.reserve(path.size());

            for (int32_t v : path)
            {
                int idx = -1;
                for (int i = 0; i < n; ++i)
                {
                    if (pv[i] == v)
                    {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0)
                    return {};
                out.push_back(mpv[idx]);
            }

            return out;
        };

        const int32_t p1 = mesh.clone_poly(poly, path1);
        const int32_t p2 = mesh.clone_poly(poly, path2);

        for (const PolyMapInfo& pm : polyMaps)
        {
            if (!pm.valid)
                continue;

            SysPolyVerts m1 = buildMapped(path1, pm.mpv);
            SysPolyVerts m2 = buildMapped(path2, pm.mpv);

            if (!m1.empty())
                mesh.map_create_poly(pm.map, p1, m1);
            if (!m2.empty())
                mesh.map_create_poly(pm.map, p2, m2);

            mesh.map_remove_poly(pm.map, poly);
        }

        mesh.remove_poly(poly);
        return {p1, p2};
    }

    // ------------------------------------------------------------
    // Build a list of edges to connect, based on current selection mode.
    // - EDGES: selected edges only
    // - POLYS: outline edges of selected polys (count==1 among selection)
    // - VERTS: no-op (for now)
    // ------------------------------------------------------------
    static std::vector<IndexPair> build_edges_to_connect(SysMesh* mesh, SelectionMode mode)
    {
        if (!mesh)
            return {};

        // EDGES mode: strict selection only
        if (mode == SelectionMode::EDGES)
        {
            const auto& selEdges = mesh->selected_edges();
            if (selEdges.empty())
                return {};
            return selEdges;
        }

        // POLYS mode: derive outline edges from selected polys
        // POLYS mode: derive INTERNAL edges from selected polys (count==2 among selection)
        if (mode == SelectionMode::POLYS)
        {
            const auto& selPolys = mesh->selected_polys();
            if (selPolys.empty())
                return {};

            std::unordered_map<IndexPair, int32_t, EdgeHash> counts = {};
            counts.reserve(selPolys.size() * 8);

            for (int32_t pi : selPolys)
            {
                if (!mesh->poly_valid(pi))
                    continue;

                const SysPolyEdges pe = mesh->poly_edges(pi);
                for (const IndexPair& eIn : pe)
                {
                    const IndexPair e = SysMesh::sort_edge(eIn);
                    if (!mesh->vert_valid(e.first) || !mesh->vert_valid(e.second))
                        continue;

                    counts[e] += 1;
                }
            }

            std::vector<IndexPair> out = {};
            out.reserve(counts.size());

            // INTERNAL edges only: appear twice among selected polys
            for (const auto& [e, c] : counts)
            {
                if (c == 2)
                    out.push_back(e);
            }

            return out;
        }

        // VERTS mode: not supported in this command version
        return {};
    }

} // namespace

bool CmdConnect::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool any = false;

    const SelectionMode mode = scene->selectionMode();

    for (SysMesh* mesh : scene->activeMeshes())
    {
        if (!mesh)
            continue;

        const std::vector<int32_t> maps = collect_maps_to_preserve(mesh);

        std::vector<IndexPair> edgesToConnect = build_edges_to_connect(mesh, mode);
        if (edgesToConnect.empty())
            continue;

        std::unordered_set<int32_t> selectedPolySet = {};

        if (mode == SelectionMode::POLYS)
        {
            const auto& selp = mesh->selected_polys();
            selectedPolySet.reserve(selp.size() * 2);
            for (int32_t p : selp)
                if (mesh->poly_valid(p))
                    selectedPolySet.insert(p);
        }

        // Dedupe edges (sorted key)
        std::unordered_set<IndexPair, EdgeHash> uniqueEdges = {};
        uniqueEdges.reserve(edgesToConnect.size() * 2);

        for (const IndexPair& eIn : edgesToConnect)
        {
            const IndexPair e = SysMesh::sort_edge(eIn);
            if (!mesh->vert_valid(e.first) || !mesh->vert_valid(e.second))
                continue;

            uniqueEdges.insert(e);
        }

        if (uniqueEdges.empty())
            continue;

        std::unordered_set<int32_t> visitedPolys = {};
        visitedPolys.reserve(256);

        std::vector<IndexPair> newEdgesToSelect = {};
        newEdgesToSelect.reserve(uniqueEdges.size());

        constexpr float t = 0.5f;

        for (const IndexPair& e : uniqueEdges)
        {
            SysEdgePolys adj = mesh->edge_polys(e);

            for (int32_t pi0 : adj)
            {
                if (!mesh->poly_valid(pi0))
                    continue;

                if (visitedPolys.contains(pi0))
                    continue;

                const SysPolyVerts pv0 = mesh->poly_verts(pi0);
                if (pv0.size() != 4)
                    continue;

                const IndexPair eSorted = SysMesh::sort_edge(e);
                const int       edgeIdx = find_edge_index_in_poly(pv0, eSorted);
                if (edgeIdx < 0)
                    continue;

                const IndexPair opp0 = poly_edge_at(pv0, (edgeIdx + 2) & 3);

                auto [p1, vA] = split_edge_in_poly(*mesh, pi0, eSorted, t, maps);
                if (p1 < 0 || vA < 0)
                    continue;

                const SysPolyVerts pv1 = mesh->poly_verts(p1);
                if (pv1.size() != 5)
                    continue;

                auto [p2, vB] = split_edge_in_poly(*mesh, p1, opp0, t, maps);
                if (p2 < 0 || vB < 0)
                    continue;

                auto [c1, c2] = split_poly_connect_boundary(*mesh, p2, vA, vB, maps);
                if (c1 < 0 || c2 < 0)
                    continue;

                visitedPolys.insert(pi0);
                newEdgesToSelect.push_back(SysMesh::sort_edge({vA, vB}));
                any = true;
            }
        }

        if (any)
        {
            // Selection feedback:
            // - If we were in POLYS mode, switch edge selection so user sees the new cuts.
            // - If we were in EDGES mode, same behavior.
            mesh->clear_selected_edges();
            for (const IndexPair& ne : newEdgesToSelect)
                mesh->select_edge(ne, true);
        }
    }

    return any;
}
