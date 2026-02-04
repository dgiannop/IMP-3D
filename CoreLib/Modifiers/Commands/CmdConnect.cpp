//=============================================================================
// CmdConnect.cpp
//=============================================================================
#include "CmdConnect.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scene.hpp"
#include "SysMesh.hpp"

namespace
{
    // ------------------------------------------------------------
    // Hash for IndexPair edges
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

    // ------------------------------------------------------------
    // Map probing (like your old code: id 0..15)
    // ------------------------------------------------------------
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

    // ------------------------------------------------------------
    // Old-school "connect_poly_verts" but index-based, returns two index lists.
    // This is your 2016 helper verbatim in spirit.
    // ------------------------------------------------------------
    using CntPoly  = std::vector<int>;
    using CntPolys = std::vector<CntPoly>;

    static CntPolys connect_poly_indices(int valence, int v1, int v2)
    {
        CntPolys res = {};
        if (valence < 3 || v1 < 0 || v2 < 0 || v1 == v2)
            return res;

        int     index     = 0;
        CntPoly new_pv[2] = {};

        for (int prev = valence - 1, next = 0; next < valence; prev = next++)
        {
            if ((next == v1 || next == v2) && !(prev == v1 || prev == v2))
            {
                new_pv[index].push_back(next);
                index = index == 0 ? 1 : 0;
            }
            new_pv[index].push_back(next);
        }

        if (new_pv[0].size() > 2 && new_pv[1].size() > 2)
        {
            res.push_back(new_pv[0]);
            res.push_back(new_pv[1]);
        }

        return res;
    }

    // ------------------------------------------------------------
    // Clone a "corner map poly" by copying coordinates (like your old code did:
    // it created new map verts per corner).
    //
    // This keeps maps stable even if the old map verts were shared elsewhere.
    // ------------------------------------------------------------
    static SysPolyVerts clone_map_poly_from_indices(SysMesh&            mesh,
                                                    int32_t             map,
                                                    const SysPolyVerts& oldMapPoly,
                                                    const CntPoly&      idxList)
    {
        SysPolyVerts out = {};
        out.reserve(idxList.size());

        const int dim = mesh.map_dim(map);

        for (int i : idxList)
        {
            if (i < 0 || i >= static_cast<int>(oldMapPoly.size()))
                return {};

            const int32_t mv = oldMapPoly[i];
            const float*  p  = mesh.map_vert_position(map, mv);
            if (!p)
                return {};

            float tmp[4] = {0.f, 0.f, 0.f, 0.f};
            for (int k = 0; k < dim && k < 4; ++k)
                tmp[k] = p[k];

            out.push_back(mesh.map_create_vert(map, tmp));
        }

        return out;
    }

    // ------------------------------------------------------------
    // Connect two existing boundary verts (by vertex IDs) in a poly.
    //
    // Replaces poly with 2 new polys using clone/remove (like old poly.clone()+poly.remove()).
    // Also clones probed maps (0..15) when map poly corners are 1:1.
    //
    // Returns: {p1,p2} or {-1,-1}
    // ------------------------------------------------------------
    static std::pair<int32_t, int32_t> connect_poly_verts(SysMesh&                    mesh,
                                                          int32_t                     poly,
                                                          int32_t                     vA,
                                                          int32_t                     vB,
                                                          const std::vector<int32_t>& maps)
    {
        if (!mesh.poly_valid(poly))
            return {-1, -1};

        const SysPolyVerts pv = mesh.poly_verts(poly);
        const int          n  = static_cast<int>(pv.size());
        if (n < 3)
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

        const CntPolys splits = connect_poly_indices(n, ia, ib);
        if (splits.size() != 2)
            return {-1, -1};

        // Gather aligned map polys (only if 1:1 with base corners)
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

        auto build_geom = [&](const CntPoly& idxList) -> SysPolyVerts {
            SysPolyVerts out = {};
            out.reserve(idxList.size());
            for (int idx : idxList)
            {
                if (idx < 0 || idx >= n)
                    return {};
                out.push_back(pv[idx]);
            }
            return out;
        };

        const SysPolyVerts pv1 = build_geom(splits[0]);
        const SysPolyVerts pv2 = build_geom(splits[1]);
        if (pv1.size() < 3 || pv2.size() < 3)
            return {-1, -1};

        const int32_t p1 = mesh.clone_poly(poly, pv1);
        const int32_t p2 = mesh.clone_poly(poly, pv2);

        // Clone maps per corner (like your old code)
        for (const PolyMapInfo& pm : polyMaps)
        {
            if (!pm.valid)
                continue;

            const SysPolyVerts mp1 = clone_map_poly_from_indices(mesh, pm.map, pm.mpv, splits[0]);
            const SysPolyVerts mp2 = clone_map_poly_from_indices(mesh, pm.map, pm.mpv, splits[1]);

            if (mp1.size() == pv1.size())
                mesh.map_create_poly(pm.map, p1, mp1);
            if (mp2.size() == pv2.size())
                mesh.map_create_poly(pm.map, p2, mp2);

            mesh.map_remove_poly(pm.map, poly);
        }

        mesh.remove_poly(poly);
        return {p1, p2};
    }

    // ------------------------------------------------------------
    // Edge midpoint cache (the SysMesh equivalent of your old AutoWelder use here).
    //
    // Key point: ONE midpoint vertex per geometric edge -> real connectivity.
    // ------------------------------------------------------------
    static int32_t get_or_create_midpoint(SysMesh&                                          mesh,
                                          const IndexPair&                                  eIn,
                                          float                                             t,
                                          std::unordered_map<IndexPair, int32_t, EdgeHash>& midCache)
    {
        const IndexPair e = SysMesh::sort_edge(eIn);

        if (auto it = midCache.find(e); it != midCache.end())
            return it->second;

        if (!mesh.vert_valid(e.first) || !mesh.vert_valid(e.second))
            return -1;

        const glm::vec3 pa = mesh.vert_position(e.first);
        const glm::vec3 pb = mesh.vert_position(e.second);
        const glm::vec3 pm = glm::mix(pa, pb, t);

        const int32_t vm = mesh.create_vert(pm);
        midCache.emplace(e, vm);
        return vm;
    }

    // ------------------------------------------------------------
    // Insert an existing midpoint vertex (vm) into a polygon along edge (a,b).
    // Rebuilds the poly via clone/remove, and clones maps per corner.
    //
    // Returns: newPolyId on success (poly becomes this), or -1 on failure.
    //
    // NOTE: This is intentionally "old-style":
    // - Only rewrites THIS poly.
    // - Neighbor polys will also insert the SAME vm later because of midCache,
    //   exactly like your old split_edge + AutoWelder behavior.
    // ------------------------------------------------------------
    static int32_t split_edge_in_poly(SysMesh&                    mesh,
                                      int32_t                     poly,
                                      const IndexPair&            eIn,
                                      int32_t                     vm,
                                      float                       t,
                                      const std::vector<int32_t>& maps)
    {
        if (!mesh.poly_valid(poly))
            return -1;

        const IndexPair e = SysMesh::sort_edge(eIn);

        const SysPolyVerts pv = mesh.poly_verts(poly);
        const int          n  = static_cast<int>(pv.size());
        if (n < 3)
            return -1;

        // Find edge index
        int edgeIdx = -1;
        for (int i = 0; i < n; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) % n];
            if (SysMesh::sort_edge({a, b}) == e)
            {
                edgeIdx = i;
                break;
            }
        }
        if (edgeIdx < 0)
            return -1;

        // Gather aligned map polys
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

        // Build new poly verts with vm inserted after pv[edgeIdx]
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
            nv.push_back(pv[i]);

            for (size_t mi = 0; mi < polyMaps.size(); ++mi)
            {
                if (polyMaps[mi].valid)
                    newMapPolys[mi].push_back(polyMaps[mi].mpv[i]);
            }

            if (i == edgeIdx)
            {
                nv.push_back(vm);

                // Insert per-poly map midpoint (lerp endpoints)
                for (size_t mi = 0; mi < polyMaps.size(); ++mi)
                {
                    if (!polyMaps[mi].valid)
                        continue;

                    const int32_t map = polyMaps[mi].map;

                    const int32_t mv0 = polyMaps[mi].mpv[i];
                    const int32_t mv1 = polyMaps[mi].mpv[(i + 1) % n];

                    const float* a0  = mesh.map_vert_position(map, mv0);
                    const float* a1  = mesh.map_vert_position(map, mv1);
                    const int    dim = mesh.map_dim(map);

                    float tmp[4] = {0.f, 0.f, 0.f, 0.f};
                    if (a0 && a1)
                    {
                        for (int k = 0; k < dim && k < 4; ++k)
                            tmp[k] = a0[k] + (a1[k] - a0[k]) * t;
                    }

                    newMapPolys[mi].push_back(mesh.map_create_vert(map, tmp));
                }
            }
        }

        const int32_t newPoly = mesh.clone_poly(poly, nv);

        for (size_t mi = 0; mi < polyMaps.size(); ++mi)
        {
            if (!polyMaps[mi].valid)
                continue;

            // Clone per-corner map verts (like old code did) for stability
            // We already inserted a fresh map vert for the midpoint, but we still want
            // per-poly uniqueness across all corners of the new poly, so recreate them.
            //
            // If you prefer reuse (lighter), replace this with direct map_create_poly(...)
            // using newMapPolys[mi].
            SysPolyVerts cloned = {};
            cloned.reserve(newMapPolys[mi].size());

            const int map = polyMaps[mi].map;
            const int dim = mesh.map_dim(map);

            for (int32_t mv : newMapPolys[mi])
            {
                const float* p      = mesh.map_vert_position(map, mv);
                float        tmp[4] = {0.f, 0.f, 0.f, 0.f};

                if (p)
                {
                    for (int k = 0; k < dim && k < 4; ++k)
                        tmp[k] = p[k];
                }

                cloned.push_back(mesh.map_create_vert(map, tmp));
            }

            mesh.map_create_poly(map, newPoly, cloned);
            mesh.map_remove_poly(map, poly);
        }

        mesh.remove_poly(poly);
        return newPoly;
    }

    // ------------------------------------------------------------
    // EDGE-mode core (ported from your old code):
    // For each poly that has >=2 selected edges:
    //   split those selected edges (midpoints),
    //   connect the two midpoints inside the poly.
    // ------------------------------------------------------------
    static bool connect_selected_edges_in_mesh(SysMesh& mesh, const std::vector<int32_t>& maps)
    {
        const auto& selEdges = mesh.selected_edges();
        if (selEdges.empty())
            return false;

        std::unordered_set<IndexPair, EdgeHash> selSet = {};
        selSet.reserve(selEdges.size() * 2);

        for (const IndexPair& eIn : selEdges)
            selSet.insert(SysMesh::sort_edge(eIn));

        // Unique polys touched by selected edges (like old IndexSet logic)
        std::unordered_set<int32_t> polySet = {};
        polySet.reserve(selEdges.size() * 2);

        for (const IndexPair& eIn : selEdges)
        {
            const IndexPair e   = SysMesh::sort_edge(eIn);
            SysEdgePolys    adj = mesh.edge_polys(e);

            for (int32_t pi : adj)
                if (mesh.poly_valid(pi))
                    polySet.insert(pi);
        }

        std::vector<int32_t> polys = {};
        polys.reserve(polySet.size());

        for (int32_t p : polySet)
            polys.push_back(p);

        // Midpoint cache = AutoWelder equivalent for this tool
        std::unordered_map<IndexPair, int32_t, EdgeHash> midCache = {};
        midCache.reserve(selSet.size() * 2);

        bool            any = false;
        constexpr float t   = 0.5f;

        for (int32_t poly0 : polys)
        {
            int32_t poly = poly0;
            if (!mesh.poly_valid(poly))
                continue;

            // Find first 2 selected edges that belong to this poly (same as old code)
            std::vector<IndexPair> chosen = {};
            chosen.reserve(2);

            const SysPolyEdges pe = mesh.poly_edges(poly);
            for (const IndexPair& eIn : pe)
            {
                const IndexPair e = SysMesh::sort_edge(eIn);
                if (selSet.contains(e))
                {
                    chosen.push_back(e);
                    if (chosen.size() == 2)
                        break;
                }
            }

            if (chosen.size() < 2)
                continue;

            // Split first selected edge
            const int32_t v0 = get_or_create_midpoint(mesh, chosen[0], t, midCache);
            if (v0 < 0)
                continue;

            poly = split_edge_in_poly(mesh, poly, chosen[0], v0, t, maps);
            if (poly < 0 || !mesh.poly_valid(poly))
                continue;

            // Split second selected edge (still by original endpoints)
            const int32_t v1 = get_or_create_midpoint(mesh, chosen[1], t, midCache);
            if (v1 < 0)
                continue;

            poly = split_edge_in_poly(mesh, poly, chosen[1], v1, t, maps);
            if (poly < 0 || !mesh.poly_valid(poly))
                continue;

            // Now connect the two midpoints inside this (new) polygon
            auto [p1, p2] = connect_poly_verts(mesh, poly, v0, v1, maps);
            if (p1 >= 0 && p2 >= 0)
                any = true;
        }

        return any;
    }

    // ------------------------------------------------------------
    // VERT-mode (ported: connect first/last selected verts within a poly)
    // ------------------------------------------------------------
    static bool connect_selected_verts_in_mesh(SysMesh& mesh, const std::vector<int32_t>& maps)
    {
        const auto& selVerts = mesh.selected_verts();
        if (selVerts.size() < 2)
            return false;

        // Candidate polys: those that have >=2 selected verts
        std::unordered_set<int32_t> polySet = {};
        polySet.reserve(selVerts.size() * 2);

        for (int32_t v : selVerts)
        {
            if (!mesh.vert_valid(v))
                continue;

            const SysVertPolys vp = mesh.vert_polys(v);
            for (int32_t p : vp)
                if (mesh.poly_valid(p))
                    polySet.insert(p);
        }

        bool any = false;

        for (int32_t poly : polySet)
        {
            if (!mesh.poly_valid(poly))
                continue;

            const SysPolyVerts pv = mesh.poly_verts(poly);

            std::vector<int32_t> picked = {};
            picked.reserve(4);

            for (int32_t v : pv)
            {
                if (mesh.vert_selected(v))
                    picked.push_back(v);
            }

            if (picked.size() < 2)
                continue;

            // Old behavior: connect selection.front() and selection.back()
            auto [p1, p2] = connect_poly_verts(mesh, poly, picked.front(), picked.back(), maps);
            if (p1 >= 0 && p2 >= 0)
                any = true;
        }

        return any;
    }

    // ------------------------------------------------------------
    // POLY-mode (simple fallback port):
    // Treat internal edges (count==2 among selected polys) as "selected edges"
    // and run EDGE-mode core on them by temporarily selecting those edges.
    //
    // This is not the full 2016 strip/loop grouping logic, but it preserves
    // the "select polys -> connect along shared structure" feel without
    // complex ordering dependencies.
    // ------------------------------------------------------------
    static bool connect_selected_polys_simple(SysMesh& mesh, const std::vector<int32_t>& maps)
    {
        const auto& selPolys = mesh.selected_polys();
        if (selPolys.empty())
            return false;

        std::unordered_map<IndexPair, int32_t, EdgeHash> edgeCounts = {};
        edgeCounts.reserve(selPolys.size() * 8);

        for (int32_t p : selPolys)
        {
            if (!mesh.poly_valid(p))
                continue;

            const SysPolyEdges pe = mesh.poly_edges(p);
            for (const IndexPair& eIn : pe)
            {
                const IndexPair e = SysMesh::sort_edge(eIn);
                edgeCounts[e] += 1;
            }
        }

        // Build internal edges list (count==2)
        std::vector<IndexPair> internalEdges = {};
        internalEdges.reserve(edgeCounts.size());

        for (const auto& [e, c] : edgeCounts)
        {
            if (c == 2)
                internalEdges.push_back(e);
        }

        if (internalEdges.empty())
            return false;

        // Temporarily set edge selection to these edges
        const auto oldSel = mesh.selected_edges();

        mesh.clear_selected_edges();
        for (const IndexPair& e : internalEdges)
            mesh.select_edge(e, true);

        const bool any = connect_selected_edges_in_mesh(mesh, maps);

        // Restore original selection
        mesh.clear_selected_edges();
        for (const IndexPair& e : oldSel)
            mesh.select_edge(e, true);

        return any;
    }

} // namespace

bool CmdConnect::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool any = false;

    const SelectionMode mode = scene->selectionMode();

    for (SysMesh* meshPtr : scene->activeMeshes())
    {
        if (!meshPtr)
            continue;

        SysMesh& mesh = *meshPtr;

        const std::vector<int32_t> maps = collect_maps_to_preserve(&mesh);

        if (mode == SelectionMode::VERTS)
        {
            any |= connect_selected_verts_in_mesh(mesh, maps);
        }
        else if (mode == SelectionMode::EDGES)
        {
            any |= connect_selected_edges_in_mesh(mesh, maps);
        }
        else if (mode == SelectionMode::POLYS)
        {
            any |= connect_selected_polys_simple(mesh, maps);
        }
    }

    // if (scene)
    // scene->report("Connect command", true);

    return any;
}
