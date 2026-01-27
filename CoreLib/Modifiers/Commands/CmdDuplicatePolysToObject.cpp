#include "CmdDuplicatePolysToObject.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    static int ensure_dst_map_if_src_has(SysMesh* src,
                                         SysMesh* dst,
                                         int32_t  mapId,
                                         int      expectedDim) noexcept
    {
        if (!src || !dst)
            return -1;

        const int srcMap = src->map_find(mapId);
        if (srcMap < 0)
            return -1;

        if (src->map_dim(srcMap) != expectedDim)
            return -1;

        const int dstMap = dst->map_find(mapId);
        if (dstMap >= 0)
            return (dst->map_dim(dstMap) == expectedDim) ? dstMap : -1;

        return dst->map_create(mapId, /*flags*/ 0, expectedDim);
    }

    static bool copy_face_varying_map(SysMesh*            srcMesh,
                                      SysMesh*            dstMesh,
                                      int32_t             mapId,
                                      int                 expectedDim,
                                      int32_t             srcPid,
                                      int32_t             dstPid,
                                      const SysPolyVerts& dstPolyVerts) noexcept
    {
        if (!srcMesh || !dstMesh)
            return false;

        const int srcMap = srcMesh->map_find(mapId);
        const int dstMap = dstMesh->map_find(mapId);
        if (srcMap < 0 || dstMap < 0)
            return false;

        if (srcMesh->map_dim(srcMap) != expectedDim || dstMesh->map_dim(dstMap) != expectedDim)
            return false;

        if (!srcMesh->map_poly_valid(srcMap, srcPid))
            return false;

        const SysPolyVerts& srcMv = srcMesh->map_poly_verts(srcMap, srcPid);
        if (srcMv.size() != dstPolyVerts.size())
            return false;

        SysPolyVerts dstMv = {};
        dstMv.reserve(srcMv.size());

        for (int32_t mv : srcMv)
        {
            const float* p = srcMesh->map_vert_position(srcMap, mv);
            if (!p)
                return false;

            float tmp[4] = {0, 0, 0, 0};
            for (int i = 0; i < expectedDim; ++i)
                tmp[i] = p[i];

            const int32_t newMv = dstMesh->map_create_vert(dstMap, tmp);
            if (newMv < 0)
                return false;

            dstMv.push_back(newMv);
        }

        dstMesh->map_create_poly(dstMap, dstPid, dstMv);
        return true;
    }

    static void clear_component_selection(Scene* scene) noexcept
    {
        if (!scene)
            return;

        for (SceneMesh* sm : scene->sceneMeshes())
        {
            if (!sm)
                continue;

            SysMesh* mesh = sm->sysMesh();
            if (!mesh)
                continue;

            mesh->clear_selected_verts();
            mesh->clear_selected_edges();
            mesh->clear_selected_polys();
        }
    }
} // namespace

bool CmdDuplicatePolysToObject::execute(Scene* scene)
{
    if (!scene)
        return false;

    // Snapshot source meshes (we will append new meshes).
    const std::vector<SceneMesh*> srcMeshes = scene->sceneMeshes();

    // ------------------------------------------------------------
    // 1) Scene-wide: does ANY source mesh have selected polys?
    // ------------------------------------------------------------
    bool anySelectedPolys = false;
    for (SceneMesh* sm : srcMeshes)
    {
        if (!sm)
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        if (!mesh->selected_polys().empty())
        {
            anySelectedPolys = true;
            break;
        }
    }

    bool any = false;

    // We'll end with only the duplicates selected (DCC-ish).
    clear_component_selection(scene);

    for (SceneMesh* srcSm : srcMeshes)
    {
        if (!srcSm)
            continue;

        SysMesh* srcMesh = srcSm->sysMesh();
        if (!srcMesh)
            continue;

        // ------------------------------------------------------------
        // 2) Determine polys to duplicate for THIS mesh
        // ------------------------------------------------------------
        std::vector<int32_t> polys = {};

        if (anySelectedPolys)
        {
            const auto& selp = srcMesh->selected_polys();
            if (selp.empty())
                continue;

            polys.assign(selp.begin(), selp.end());
        }
        else
        {
            const auto& allp = srcMesh->all_polys();
            polys.assign(allp.begin(), allp.end());
        }

        // Filter valid polys
        {
            std::vector<int32_t> valid = {};
            valid.reserve(polys.size());
            for (int32_t pid : polys)
                if (srcMesh->poly_valid(pid))
                    valid.push_back(pid);
            polys = std::move(valid);
        }

        if (polys.empty())
            continue;

        // ------------------------------------------------------------
        // 3) Create destination mesh object
        // ------------------------------------------------------------
        SceneMesh* dstSm = scene->createSceneMesh();
        if (!dstSm)
            continue;

        SysMesh* dstMesh = dstSm->sysMesh();
        if (!dstMesh)
            continue;

        // Create dst maps only if src has them.
        (void)ensure_dst_map_if_src_has(srcMesh, dstMesh, /*mapId*/ 0, /*dim*/ 3); // normals
        (void)ensure_dst_map_if_src_has(srcMesh, dstMesh, /*mapId*/ 1, /*dim*/ 2); // uvs

        // src vert -> dst vert
        std::unordered_map<int32_t, int32_t> vDup = {};
        vDup.reserve(polys.size() * 4ull);

        std::vector<int32_t> newPolys = {};
        newPolys.reserve(polys.size());

        // ------------------------------------------------------------
        // 4) Duplicate needed vertices
        // ------------------------------------------------------------
        for (int32_t pid : polys)
        {
            const SysPolyVerts& pv = srcMesh->poly_verts(pid);
            if (pv.size() < 3)
                continue;

            for (int32_t vi : pv)
            {
                if (!srcMesh->vert_valid(vi))
                    continue;

                if (vDup.find(vi) != vDup.end())
                    continue;

                const glm::vec3 pos   = srcMesh->vert_position(vi);
                const int32_t   newVi = dstMesh->create_vert(pos);
                if (newVi >= 0)
                    vDup.emplace(vi, newVi);
            }
        }

        if (vDup.empty())
            continue;

        // ------------------------------------------------------------
        // 5) Duplicate polygons (+ material) and maps
        // ------------------------------------------------------------
        for (int32_t pid : polys)
        {
            const SysPolyVerts& srcPv = srcMesh->poly_verts(pid);
            if (srcPv.size() < 3)
                continue;

            SysPolyVerts dstPv = {};
            dstPv.reserve(srcPv.size());

            bool ok = true;
            for (int32_t vi : srcPv)
            {
                if (!srcMesh->vert_valid(vi))
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

            const uint32_t mat    = srcMesh->poly_material(pid);
            const int32_t  newPid = dstMesh->create_poly(dstPv, mat);
            if (newPid < 0)
                continue;

            (void)copy_face_varying_map(srcMesh, dstMesh, /*mapId*/ 1, /*dim*/ 2, pid, newPid, dstPv);
            (void)copy_face_varying_map(srcMesh, dstMesh, /*mapId*/ 0, /*dim*/ 3, pid, newPid, dstPv);

            newPolys.push_back(newPid);
        }

        if (newPolys.empty())
            continue;

        // ------------------------------------------------------------
        // 6) Select new polys (accumulate selection across duplicates)
        // ------------------------------------------------------------
        for (int32_t pid : newPolys)
            if (dstMesh->poly_valid(pid))
                dstMesh->select_poly(pid, true);

        any = true;
    }

    return any;
}
