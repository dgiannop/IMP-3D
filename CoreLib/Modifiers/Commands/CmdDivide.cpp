// CmdDivide.cpp

#include "CmdDivide.hpp"

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
    static uint64_t pack_undirected_edge(int32_t a, int32_t b) noexcept
    {
        if (a > b)
            std::swap(a, b);

        const uint64_t ua = static_cast<uint64_t>(static_cast<uint32_t>(a));
        const uint64_t ub = static_cast<uint64_t>(static_cast<uint32_t>(b));
        return (ua << 32) | ub;
    }

    static glm::vec3 average_positions(const SysMesh* mesh, const SysPolyVerts& pv) noexcept
    {
        glm::vec3 c = glm::vec3(0.0f);
        int       n = 0;

        for (int32_t v : pv)
        {
            if (!mesh->vert_valid(v))
                continue;

            c += mesh->vert_position(v);
            ++n;
        }

        if (n > 0)
            c *= (1.0f / static_cast<float>(n));

        return c;
    }

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

    static std::vector<float> read_map_vec(const SysMesh* mesh, int32_t map, int32_t mv)
    {
        const int32_t dim = mesh->map_dim(map);

        std::vector<float> out;
        out.resize(std::max(0, dim), 0.0f);

        const float* p = mesh->map_vert_position(map, mv);
        if (!p || dim <= 0)
            return out;

        for (int32_t i = 0; i < dim; ++i)
            out[i] = p[i];

        return out;
    }

    static int32_t create_map_vert_lerp(SysMesh* mesh,
                                        int32_t  map,
                                        int32_t  mvA,
                                        int32_t  mvB,
                                        float    t)
    {
        const int32_t dim = mesh->map_dim(map);
        if (dim <= 0)
            return -1;

        const float* a = mesh->map_vert_position(map, mvA);
        const float* b = mesh->map_vert_position(map, mvB);
        if (!a || !b)
            return -1;

        std::vector<float> tmp;
        tmp.resize(dim, 0.0f);

        for (int32_t i = 0; i < dim; ++i)
            tmp[i] = a[i] + (b[i] - a[i]) * t;

        return mesh->map_create_vert(map, tmp.data());
    }

    static int32_t create_map_vert_average(SysMesh*            mesh,
                                           int32_t             map,
                                           const SysPolyVerts& mapPolyVerts)
    {
        const int32_t dim = mesh->map_dim(map);
        if (dim <= 0)
            return -1;

        std::vector<float> tmp;
        tmp.resize(dim, 0.0f);

        int count = 0;
        for (int32_t mv : mapPolyVerts)
        {
            const float* p = mesh->map_vert_position(map, mv);
            if (!p)
                continue;

            for (int32_t i = 0; i < dim; ++i)
                tmp[i] += p[i];

            ++count;
        }

        if (count > 0)
        {
            const float inv = 1.0f / static_cast<float>(count);
            for (int32_t i = 0; i < dim; ++i)
                tmp[i] *= inv;
        }

        return mesh->map_create_vert(map, tmp.data());
    }

    static std::vector<int32_t> build_polys_to_divide(SysMesh* mesh)
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

bool CmdDivide::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool any = false;

    for (SysMesh* mesh : scene->activeMeshes())
    {
        if (!mesh)
            continue;

        std::vector<int32_t> polysToDivide = build_polys_to_divide(mesh);
        if (polysToDivide.empty())
            continue;

        // Filter invalid and degenerate polys up-front.
        polysToDivide.erase(std::remove_if(polysToDivide.begin(),
                                           polysToDivide.end(),
                                           [&](int32_t p) {
                                               if (!mesh->poly_valid(p))
                                                   return true;
                                               const SysPolyVerts& pv = mesh->poly_verts(p);
                                               return pv.size() < 3;
                                           }),
                            polysToDivide.end());

        if (polysToDivide.empty())
            continue;

        // Maps to preserve (UVs, normals, etc.)
        const std::vector<int32_t> maps = collect_maps_to_preserve(mesh);

        // Geometry midpoint cache (shared across adjacent subdivided polys).
        std::unordered_map<uint64_t, int32_t> midVertCache;
        midVertCache.reserve(polysToDivide.size() * 8);

        // Remove originals at end.
        std::vector<int32_t> removePolys;
        removePolys.reserve(polysToDivide.size());

        for (int32_t poly : polysToDivide)
        {
            if (!mesh->poly_valid(poly))
                continue;

            const SysPolyVerts pv = mesh->poly_verts(poly);
            const int          n  = static_cast<int>(pv.size());
            if (n < 3)
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

            // --- Create / fetch geometric edge midpoints ---
            std::vector<int32_t> mids;
            mids.resize(n, -1);

            for (int i = 0; i < n; ++i)
            {
                const int32_t a = pv[i];
                const int32_t b = pv[(i + 1) % n];

                const uint64_t key = pack_undirected_edge(a, b);

                auto it = midVertCache.find(key);
                if (it != midVertCache.end() && mesh->vert_valid(it->second))
                {
                    mids[i] = it->second;
                    continue;
                }

                const glm::vec3 pa = mesh->vert_position(a);
                const glm::vec3 pb = mesh->vert_position(b);
                const glm::vec3 pm = (pa + pb) * 0.5f;

                const int32_t vm = mesh->create_vert(pm);
                mids[i]          = vm;
                midVertCache.emplace(key, vm);
            }

            // --- Create center vertex (not shared) ---
            const glm::vec3 centerPos = average_positions(mesh, pv);
            const int32_t   vCenter   = mesh->create_vert(centerPos);

            // --- Prepare map poly-verts for this polygon (per map) ---
            struct PolyMapInfo
            {
                int32_t      map = -1;
                SysPolyVerts mpv;
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

            // --- Create n quads ---
            // quad i: [v_i, mid(i), center, mid(i-1)]
            for (int i = 0; i < n; ++i)
            {
                const int iprev = (i + n - 1) % n;

                SysPolyVerts q;
                q.push_back(pv[i]);
                q.push_back(mids[i]);
                q.push_back(vCenter);
                q.push_back(mids[iprev]);

                const int32_t newPoly = mesh->create_poly(q, mat);

                // Rebuild maps for this quad (face-varying, per-corner)
                for (const PolyMapInfo& pm : polyMaps)
                {
                    if (!pm.valid)
                        continue;

                    const int32_t map = pm.map;

                    // Original corner map verts aligned with pv order.
                    const int32_t mv_i      = pm.mpv[i];
                    const int32_t mv_inext  = pm.mpv[(i + 1) % n];
                    const int32_t mv_iprev  = pm.mpv[iprev];
                    const int32_t mv_iprevN = pm.mpv[(iprev + 1) % n]; // = pm.mpv[i] actually, but keep explicit

                    // Midpoints in map-space (per face):
                    // mid(i) uses endpoints (i, i+1)
                    // mid(iprev) uses endpoints (iprev, iprev+1)
                    const int32_t mv_mid_i     = create_map_vert_lerp(mesh, map, mv_i, mv_inext, 0.5f);
                    const int32_t mv_mid_iprev = create_map_vert_lerp(mesh, map, mv_iprev, mv_iprevN, 0.5f);

                    // Center in map-space = average of all corners of original poly
                    const int32_t mv_center = create_map_vert_average(mesh, map, pm.mpv);

                    if (mv_mid_i < 0 || mv_mid_iprev < 0 || mv_center < 0)
                        continue;

                    SysPolyVerts qmv;
                    qmv.push_back(mv_i);
                    qmv.push_back(mv_mid_i);
                    qmv.push_back(mv_center);
                    qmv.push_back(mv_mid_iprev);

                    mesh->map_create_poly(map, newPoly, qmv);
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
