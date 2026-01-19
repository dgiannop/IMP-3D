// CmdMergeByDistance.cpp

#include "CmdMergeByDistance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/gtx/norm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    constexpr float kWeldDistance = 1e-4f; // TODO: expose as UI property later

    struct CellKey
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;

        bool operator==(const CellKey& o) const noexcept
        {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct CellKeyHash
    {
        size_t operator()(const CellKey& k) const noexcept
        {
            // Simple hash combine
            size_t h   = 1469598103934665603ull;
            auto   mix = [&](uint32_t v) {
                h ^= (size_t)v + 0x9e3779b9 + (h << 6) + (h >> 2);
            };
            mix((uint32_t)k.x);
            mix((uint32_t)k.y);
            mix((uint32_t)k.z);
            return h;
        }
    };

    static CellKey cell_of(const glm::vec3& p, float cellSize) noexcept
    {
        const float inv = (cellSize > 0.0f) ? (1.0f / cellSize) : 1.0f;

        auto fi = [&](float v) -> int32_t {
            // floor to int
            return (int32_t)std::floor(v * inv);
        };

        CellKey k;
        k.x = fi(p.x);
        k.y = fi(p.y);
        k.z = fi(p.z);
        return k;
    }

    static std::vector<int32_t> collect_maps_to_preserve(const SysMesh* mesh)
    {
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

    static std::vector<int32_t> build_target_verts(SysMesh* mesh)
    {
        if (!mesh)
            return {};

        // 1) Selected verts
        const auto& selV = mesh->selected_verts();
        if (!selV.empty())
            return selV;

        // 2) Selected polys -> collect their verts
        const auto& selP = mesh->selected_polys();
        if (!selP.empty())
        {
            std::unordered_set<int32_t> verts;
            verts.reserve(selP.size() * 4);

            for (int32_t p : selP)
            {
                if (!mesh->poly_valid(p))
                    continue;

                const SysPolyVerts& pv = mesh->poly_verts(p);
                for (int32_t v : pv)
                    if (mesh->vert_valid(v))
                        verts.insert(v);
            }

            std::vector<int32_t> out;
            out.reserve(verts.size());
            for (int32_t v : verts)
                out.push_back(v);
            return out;
        }

        // 3) All verts
        return mesh->all_verts();
    }

    static void remove_consecutive_dupes(SysPolyVerts& pv, std::vector<int>& keptCornerIdx) noexcept
    {
        // pv and keptCornerIdx are parallel (same size on entry).
        if (pv.size() < 2)
            return;

        SysPolyVerts     newPv = {};
        std::vector<int> newKeep;

        newPv.reserve(pv.size());
        newKeep.reserve(keptCornerIdx.size());

        for (int i = 0; i < (int)pv.size(); ++i)
        {
            const int32_t v = pv[i];

            if (!newPv.empty() && newPv.back() == v)
                continue;

            newPv.push_back(v);
            newKeep.push_back(keptCornerIdx[(size_t)i]);
        }

        // If closed loop duplicates (first == last), drop last
        if (newPv.size() >= 2 && newPv.front() == newPv.back())
        {
            newPv.pop_back();
            newKeep.pop_back();
        }

        pv.swap(newPv);
        keptCornerIdx.swap(newKeep);
    }

} // namespace

bool CmdMergeByDistance::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool any = false;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->selected())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        const float d  = kWeldDistance;
        const float d2 = d * d;

        std::vector<int32_t> targets = build_target_verts(mesh);

        // Filter invalid
        targets.erase(std::remove_if(targets.begin(),
                                     targets.end(),
                                     [&](int32_t v) { return !mesh->vert_valid(v); }),
                      targets.end());

        if (targets.size() < 2)
            continue;

        // Maps to preserve (we won't weld map verts; only rebuild map polys if corner count changes)
        const std::vector<int32_t> maps = collect_maps_to_preserve(mesh);

        // Spatial hash: cell -> list of "kept" verts in that cell
        std::unordered_map<CellKey, std::vector<int32_t>, CellKeyHash> grid;
        grid.reserve(targets.size());

        // old -> keep
        std::unordered_map<int32_t, int32_t> weldTo;
        weldTo.reserve(targets.size());

        auto add_keep = [&](int32_t keepV) {
            const glm::vec3 p = mesh->vert_position(keepV);
            const CellKey   c = cell_of(p, d);
            grid[c].push_back(keepV);
        };

        auto find_keep_for = [&](int32_t v) -> int32_t {
            const glm::vec3 p = mesh->vert_position(v);
            const CellKey   c = cell_of(p, d);

            // Search this cell + 26 neighbors
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        CellKey q = c;
                        q.x += dx;
                        q.y += dy;
                        q.z += dz;

                        auto it = grid.find(q);
                        if (it == grid.end())
                            continue;

                        for (int32_t keepV : it->second)
                        {
                            if (!mesh->vert_valid(keepV))
                                continue;

                            const glm::vec3 pk = mesh->vert_position(keepV);
                            if (glm::length2(pk - p) <= d2)
                                return keepV;
                        }
                    }
                }
            }

            return -1;
        };

        // Build weld mapping
        for (int32_t v : targets)
        {
            if (!mesh->vert_valid(v))
                continue;

            if (weldTo.find(v) != weldTo.end())
                continue;

            const int32_t keep = find_keep_for(v);
            if (keep >= 0 && keep != v)
            {
                weldTo.emplace(v, keep);
                any = true;
                continue;
            }

            // Keep itself
            weldTo.emplace(v, v);
            add_keep(v);
        }

        // If nothing mapped to a different vert, skip.
        bool hasMerge = false;
        for (const auto& [a, b] : weldTo)
        {
            if (a != b)
            {
                hasMerge = true;
                break;
            }
        }
        if (!hasMerge)
            continue;

        // --- Rewrite polys ---
        // We'll rebuild changed polys as new polys, copy material, and rebuild map polys (reusing map-vert IDs).
        const std::vector<int32_t> allPolys = mesh->all_polys();

        std::vector<int32_t> polysToRemove;
        polysToRemove.reserve(allPolys.size() / 2);

        for (int32_t pid : allPolys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts oldPv = mesh->poly_verts(pid);
            const int          nOld  = (int)oldPv.size();
            if (nOld < 3)
                continue;

            // Replace verts by weld map (if present)
            SysPolyVerts newPv = {};
            newPv.reserve(oldPv.size());

            bool changed = false;

            for (int i = 0; i < nOld; ++i)
            {
                const int32_t v = oldPv[(size_t)i];
                int32_t       r = v;

                auto it = weldTo.find(v);
                if (it != weldTo.end())
                    r = it->second;

                if (r != v)
                    changed = true;

                newPv.push_back(r);
            }

            if (!changed)
                continue;

            // Corner keep indices: start as identity mapping for map-poly reconstruction
            std::vector<int> keptCornerIdx;
            keptCornerIdx.resize(newPv.size());
            for (int i = 0; i < (int)keptCornerIdx.size(); ++i)
                keptCornerIdx[(size_t)i] = i;

            // Clean consecutive duplicates / closure duplicates
            remove_consecutive_dupes(newPv, keptCornerIdx);

            if ((int)newPv.size() < 3)
            {
                // Degenerate -> delete this poly (and its map polys)
                for (int32_t map : maps)
                {
                    if (mesh->map_poly_valid(map, pid))
                        mesh->map_remove_poly(map, pid);
                }

                polysToRemove.push_back(pid);
                any = true;
                continue;
            }

            // Create replacement poly with same material
            const uint32_t mat    = mesh->poly_material(pid);
            const int32_t  newPid = mesh->create_poly(newPv, mat);

            // Rebuild map polys by reusing existing map-vert IDs, applying the same corner removals
            for (int32_t map : maps)
            {
                if (!mesh->map_poly_valid(map, pid))
                    continue;

                const SysPolyVerts oldMpv = mesh->map_poly_verts(map, pid);
                if ((int)oldMpv.size() != nOld)
                {
                    // If mismatch, safest is: remove old mapping and skip recreating.
                    mesh->map_remove_poly(map, pid);
                    continue;
                }

                SysPolyVerts newMpv = {};
                newMpv.reserve(newPv.size());

                bool ok = true;
                for (int k : keptCornerIdx)
                {
                    if (k < 0 || k >= (int)oldMpv.size())
                    {
                        ok = false;
                        break;
                    }
                    newMpv.push_back(oldMpv[(size_t)k]);
                }

                // Remove old mapping and attach new one
                mesh->map_remove_poly(map, pid);

                if (ok && (int)newMpv.size() == (int)newPv.size() && (int)newMpv.size() >= 3)
                    mesh->map_create_poly(map, newPid, newMpv);
            }

            // Remove old poly (after maps handled)
            polysToRemove.push_back(pid);
            any = true;
        }

        // Remove old polys last (descending order is safer with HoleList-style storage)
        std::sort(polysToRemove.begin(), polysToRemove.end());
        polysToRemove.erase(std::unique(polysToRemove.begin(), polysToRemove.end()), polysToRemove.end());

        for (int i = (int)polysToRemove.size() - 1; i >= 0; --i)
        {
            const int32_t pid = polysToRemove[(size_t)i];
            if (mesh->poly_valid(pid))
                mesh->remove_poly(pid);
        }

        // Remove unused merged verts (only those that welded into another vert)
        // (We only remove if it is now unreferenced.)
        for (const auto& [v, keep] : weldTo)
        {
            if (v == keep)
                continue;

            if (!mesh->vert_valid(v))
                continue;

            // If no polys reference this vert anymore, safe to remove.
            if (mesh->vert_polys(v).empty())
                mesh->remove_vert(v);
        }

        // Clear selection because topology changed in-place
        mesh->clear_selected_verts();
        mesh->clear_selected_edges();
        mesh->clear_selected_polys();
    }

    return any;
}
