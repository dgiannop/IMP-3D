#include "CmdDuplicatePolys.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    struct IntHash
    {
        size_t operator()(int32_t v) const noexcept
        {
            return std::hash<int32_t>{}(v);
        }
    };

    static bool copy_face_varying_map(SysMesh*            mesh,
                                      int32_t             mapId,
                                      int32_t             srcPid,
                                      int32_t             dstPid,
                                      const SysPolyVerts& srcPolyVerts) noexcept
    {
        if (!mesh)
            return false;

        const int map = mesh->map_find(mapId);
        if (map < 0)
            return false;

        const int dim = mesh->map_dim(map);
        if (dim <= 0 || dim > 4)
            return false;

        if (!mesh->map_poly_valid(map, srcPid))
            return false;

        const SysPolyVerts& srcMv = mesh->map_poly_verts(map, srcPid);
        if (srcMv.size() != srcPolyVerts.size())
        {
            // If the map polygon doesn't match the face corner count, skip safely.
            return false;
        }

        SysPolyVerts dstMv = {};
        dstMv.reserve(srcMv.size());

        // Create fresh map verts per corner (face-varying, no sharing)
        for (int32_t mv : srcMv)
        {
            const float* p = mesh->map_vert_position(map, mv);
            if (!p)
                return false;

            float tmp[4] = {0, 0, 0, 0};
            for (int i = 0; i < dim; ++i)
                tmp[i] = p[i];

            const int32_t newMv = mesh->map_create_vert(map, tmp);
            if (newMv < 0)
                return false;

            dstMv.push_back(newMv);
        }

        mesh->map_create_poly(map, dstPid, dstMv);
        return true;
    }
} // namespace

bool CmdDuplicatePolys::execute(Scene* scene)
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

        // Copy target polys first (because create_poly will mutate internal buffers)
        std::vector<int32_t> srcPolys = {};
        {
            const auto& sel = mesh->selected_polys();
            if (!sel.empty())
                srcPolys.assign(sel.begin(), sel.end());
            else
                srcPolys.assign(mesh->all_polys().begin(), mesh->all_polys().end());
        }

        // Filter valid polys
        std::vector<int32_t> polys = {};
        polys.reserve(srcPolys.size());
        for (int32_t pid : srcPolys)
            if (mesh->poly_valid(pid))
                polys.push_back(pid);

        if (polys.empty())
            continue;

        // Map: original base vert -> duplicated base vert
        std::unordered_map<int32_t, int32_t, IntHash> vDup = {};
        vDup.reserve(polys.size() * 4ull);

        // Also keep a list of new polys for selection
        std::vector<int32_t> newPolys = {};
        newPolys.reserve(polys.size());

        // ---------------------------------------------------------------------
        // 1) Duplicate vertices (only those referenced by duplicated polys)
        // ---------------------------------------------------------------------
        for (int32_t pid : polys)
        {
            const SysPolyVerts& pv = mesh->poly_verts(pid);
            if (pv.size() < 3)
                continue;

            for (int32_t vi : pv)
            {
                if (!mesh->vert_valid(vi))
                    continue;

                if (vDup.find(vi) != vDup.end())
                    continue;

                const glm::vec3 pos   = mesh->vert_position(vi);
                const int32_t   newVi = mesh->create_vert(pos);
                if (newVi >= 0)
                    vDup.emplace(vi, newVi);
            }
        }

        if (vDup.empty())
            continue;

        // ---------------------------------------------------------------------
        // 2) Duplicate polygons (+ materials) and copy face-varying maps
        // ---------------------------------------------------------------------
        for (int32_t pid : polys)
        {
            const SysPolyVerts& srcPv = mesh->poly_verts(pid);
            if (srcPv.size() < 3)
                continue;

            SysPolyVerts dstPv = {};
            dstPv.reserve(srcPv.size());

            bool ok = true;
            for (int32_t vi : srcPv)
            {
                if (!mesh->vert_valid(vi))
                {
                    ok = false;
                    break;
                }

                const auto it = vDup.find(vi);
                if (it == vDup.end() || it->second < 0)
                {
                    ok = false;
                    break;
                }

                dstPv.push_back(it->second);
            }

            if (!ok || dstPv.size() < 3)
                continue;

            const uint32_t mat    = mesh->poly_material(pid);
            const int32_t  newPid = mesh->create_poly(dstPv, mat);
            if (newPid < 0)
                continue;

            // Copy face-varying UVs (map id 1) and normals (map id 0) if present
            // (fresh map verts per corner; does not share)
            (void)copy_face_varying_map(mesh, /*mapId*/ 1, pid, newPid, dstPv); // UV
            (void)copy_face_varying_map(mesh, /*mapId*/ 0, pid, newPid, dstPv); // Norm

            newPolys.push_back(newPid);
        }

        if (newPolys.empty())
            continue;

        // ---------------------------------------------------------------------
        // 3) Selection: select only the new polys
        // ---------------------------------------------------------------------
        mesh->clear_selected_verts();
        mesh->clear_selected_edges();
        mesh->clear_selected_polys();

        for (int32_t pid : newPolys)
            if (mesh->poly_valid(pid))
                mesh->select_poly(pid, true);

        any = true;
    }

    return any;
}
