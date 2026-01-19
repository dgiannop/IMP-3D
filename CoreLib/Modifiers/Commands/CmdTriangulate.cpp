// CmdTriangulate.cpp

#include "CmdTriangulate.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scene.hpp"
#include "SysMesh.hpp"

namespace
{
    static std::vector<int32_t> collect_maps_to_preserve(const SysMesh* mesh)
    {
        // SysMesh doesn’t expose "all maps", so probe a small range of IDs.
        // If you have official IDs (UV0, NRM, etc.), replace this with that list.
        std::vector<int32_t> maps;
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

    static std::vector<int32_t> build_polys_to_triangulate(SysMesh* mesh)
    {
        if (!mesh)
            return {};

        // 1) Selected polys win
        const auto& selPolys = mesh->selected_polys();
        if (!selPolys.empty())
            return selPolys;

        std::unordered_set<int32_t> polys;
        polys.reserve(256);

        // 2) Selected edges -> adjacent polys
        const auto& selEdges = mesh->selected_edges();
        if (!selEdges.empty())
        {
            for (const IndexPair& eIn : selEdges)
            {
                const IndexPair e = SysMesh::sort_edge(eIn);
                if (!mesh->vert_valid(e.first) || !mesh->vert_valid(e.second))
                    continue;

                SysEdgePolys ep = mesh->edge_polys(e);
                for (int32_t p : ep)
                    if (mesh->poly_valid(p))
                        polys.insert(p);
            }
        }

        // 3) Selected verts -> adjacent polys (only if edges had none)
        if (polys.empty())
        {
            const auto& selVerts = mesh->selected_verts();
            for (int32_t v : selVerts)
            {
                if (!mesh->vert_valid(v))
                    continue;

                const SysVertPolys& vp = mesh->vert_polys(v);
                for (int32_t p : vp)
                    if (mesh->poly_valid(p))
                        polys.insert(p);
            }
        }

        // 4) No selection at all -> all polys
        if (polys.empty())
            return mesh->all_polys();

        std::vector<int32_t> out;
        out.reserve(polys.size());
        for (int32_t p : polys)
            out.push_back(p);

        return out;
    }

} // namespace

bool CmdTriangulate::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool any = false;

    for (SysMesh* mesh : scene->activeMeshes())
    {
        if (!mesh)
            continue;

        std::vector<int32_t> polysToTri = build_polys_to_triangulate(mesh);
        if (polysToTri.empty())
            continue;

        // Filter invalid + degenerate polys up-front
        polysToTri.erase(std::remove_if(polysToTri.begin(),
                                        polysToTri.end(),
                                        [&](int32_t p) {
                                            if (!mesh->poly_valid(p))
                                                return true;
                                            const SysPolyVerts& pv = mesh->poly_verts(p);
                                            return pv.size() < 3;
                                        }),
                         polysToTri.end());

        if (polysToTri.empty())
            continue;

        // Maps to preserve (UVs, normals, etc.)
        const std::vector<int32_t> maps = collect_maps_to_preserve(mesh);

        // Remove originals at end (only those we actually triangulated)
        std::vector<int32_t> removePolys;
        removePolys.reserve(polysToTri.size());

        for (int32_t poly : polysToTri)
        {
            if (!mesh->poly_valid(poly))
                continue;

            const SysPolyVerts pv = mesh->poly_verts(poly);
            const int          n  = static_cast<int>(pv.size());
            if (n < 3)
                continue;

            // Already a triangle: nothing to do
            if (n == 3)
                continue;

            // Validate verts
            bool ok = true;
            for (int32_t v : pv)
            {
                if (!mesh->vert_valid(v))
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                continue;

            const uint32_t mat = mesh->poly_material(poly);

            // Gather per-map corner arrays for this polygon (only if aligned 1:1 with pv)
            struct PolyMapInfo
            {
                int32_t      map   = -1;
                SysPolyVerts mpv   = {};
                bool         valid = false;
            };

            std::vector<PolyMapInfo> polyMaps;
            polyMaps.reserve(maps.size());

            for (int32_t map : maps)
            {
                PolyMapInfo info{};
                info.map = map;

                if (mesh->map_poly_valid(map, poly))
                {
                    info.mpv   = mesh->map_poly_verts(map, poly);
                    info.valid = (info.mpv.size() == pv.size());
                }

                polyMaps.push_back(info);
            }

            // Fan triangulation around corner 0:
            // tri j = (0, j, j+1) for j = 1..n-2
            const int32_t v0 = pv[0];

            for (int j = 1; j + 1 < n; ++j)
            {
                SysPolyVerts tri;
                tri.push_back(v0);
                tri.push_back(pv[j]);
                tri.push_back(pv[j + 1]);

                const int32_t newPoly = mesh->create_poly(tri, mat);

                // Rebuild maps for this triangle by reusing original corner map verts
                for (const PolyMapInfo& pm : polyMaps)
                {
                    if (!pm.valid)
                        continue;

                    SysPolyVerts tmv;
                    tmv.push_back(pm.mpv[0]);
                    tmv.push_back(pm.mpv[j]);
                    tmv.push_back(pm.mpv[j + 1]);

                    mesh->map_create_poly(pm.map, newPoly, tmv);
                }

                any = true;
            }

            removePolys.push_back(poly);
        }

        // Remove original polys after we’re done creating replacements.
        for (int32_t p : removePolys)
        {
            if (mesh->poly_valid(p))
                mesh->remove_poly(p);
        }
    }

    return any;
}
