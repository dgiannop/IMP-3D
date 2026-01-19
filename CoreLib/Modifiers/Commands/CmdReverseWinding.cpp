#include "CmdReverseWinding.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    constexpr int kNormMapId = 0;
    constexpr int kUvMapId   = 1;

    static std::vector<int32_t> selected_or_all_polys(const SysMesh* mesh)
    {
        if (!mesh)
            return {};

        const auto& sel = mesh->selected_polys();
        if (!sel.empty())
            return std::vector<int32_t>(sel.begin(), sel.end());

        const auto& all = mesh->all_polys();
        return std::vector<int32_t>(all.begin(), all.end());
    }

    // Reverse winding but keep v0 fixed (more stable for some tools / debugging).
    // (v0 v1 v2 v3) -> (v0 v3 v2 v1)
    static SysPolyVerts reverse_keep_first(const SysPolyVerts& in)
    {
        SysPolyVerts out = {};
        if (in.size() < 3)
            return out;

        out.reserve(in.size());
        out.push_back(in[0]);
        for (int i = (int)in.size() - 1; i >= 1; --i)
            out.push_back(in[(size_t)i]);
        return out;
    }
} // namespace

bool CmdReverseWinding::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool any = false;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm)
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        const int normMap = mesh->map_find(kNormMapId);
        const int uvMap   = mesh->map_find(kUvMapId);

        const std::vector<int32_t> polys = selected_or_all_polys(mesh);
        if (polys.empty())
            continue;

        // If we are operating on "all polys", selection can be empty, so don’t depend on selection state.
        // But if selection exists, preserve selected state per poly.
        const auto& selPolysRef = mesh->selected_polys();

        for (int32_t pid : polys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts oldPv = mesh->poly_verts(pid);
            if (oldPv.size() < 3)
                continue;

            const uint32_t mat = mesh->poly_material(pid);

            const bool wasSelected =
                !selPolysRef.empty()
                    ? (std::find(selPolysRef.begin(), selPolysRef.end(), pid) != selPolysRef.end())
                    : false;

            // Capture mapped per-corner arrays (if present)
            SysPolyVerts oldUv = {};
            bool         hasUv = false;
            if (uvMap >= 0 && mesh->map_poly_valid(uvMap, pid))
            {
                oldUv = mesh->map_poly_verts(uvMap, pid);
                hasUv = (oldUv.size() == oldPv.size());
            }

            SysPolyVerts oldN = {};
            bool         hasN = false;
            if (normMap >= 0 && mesh->map_poly_valid(normMap, pid))
            {
                oldN = mesh->map_poly_verts(normMap, pid);
                hasN = (oldN.size() == oldPv.size());
            }

            // Build reversed arrays
            const SysPolyVerts newPv = reverse_keep_first(oldPv);
            if (newPv.size() != oldPv.size())
                continue;

            const SysPolyVerts newUv = hasUv ? reverse_keep_first(oldUv) : SysPolyVerts{};
            const SysPolyVerts newN  = hasN ? reverse_keep_first(oldN) : SysPolyVerts{};

            // Remove old map polys first (safe even if we fail recreate later).
            if (hasUv)
                mesh->map_remove_poly(uvMap, pid);
            if (hasN)
                mesh->map_remove_poly(normMap, pid);

            // Remove and recreate poly.
            // With HoleList reuse this often returns the same pid, but we still handle the general case.
            mesh->remove_poly(pid);
            const int32_t newPid = mesh->create_poly(newPv, mat);
            if (newPid < 0)
                continue;

            // Re-attach maps to the recreated poly id.
            if (hasUv && (int)newUv.size() == (int)newPv.size())
                mesh->map_create_poly(uvMap, newPid, newUv);

            if (hasN && (int)newN.size() == (int)newPv.size())
                mesh->map_create_poly(normMap, newPid, newN);

            // Restore selection on that face if it was selected.
            if (wasSelected)
                mesh->select_poly(newPid, true);

            any = true;
        }
    }

    return any;
}
