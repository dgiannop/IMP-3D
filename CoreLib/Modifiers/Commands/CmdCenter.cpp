#include "CmdCenter.hpp"

#include <algorithm>
#include <cfloat>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"
#include "glm/gtx/norm.hpp"

namespace
{
    static void collect_verts_from_polys(const SysMesh*              mesh,
                                         const std::vector<int32_t>& polys,
                                         std::vector<int32_t>&       out)
    {
        out.clear();
        out.reserve(polys.size() * 4);

        for (int32_t p : polys)
        {
            if (!mesh->poly_valid(p))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(p);
            for (int32_t v : pv)
                out.push_back(v);
        }

        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

    static void collect_all_verts(const SysMesh* mesh, std::vector<int32_t>& out)
    {
        out = mesh->all_verts();
    }
} // namespace

bool CmdCenter::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool anyChanged = false;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        // --------------------------------------------------
        // Choose working vertices
        // --------------------------------------------------
        std::vector<int32_t> verts;

        const auto& selV = mesh->selected_verts();
        const auto& selP = mesh->selected_polys();

        if (!selV.empty())
        {
            verts = selV;
        }
        else if (!selP.empty())
        {
            collect_verts_from_polys(mesh, selP, verts);
        }
        else
        {
            collect_all_verts(mesh, verts);
        }

        if (verts.empty())
            continue;

        // --------------------------------------------------
        // Compute AABB
        // --------------------------------------------------
        glm::vec3 minP(FLT_MAX);
        glm::vec3 maxP(-FLT_MAX);

        for (int32_t v : verts)
        {
            if (!mesh->vert_valid(v))
                continue;

            const glm::vec3& p = mesh->vert_position(v);
            minP               = glm::min(minP, p);
            maxP               = glm::max(maxP, p);
        }

        if (minP.x > maxP.x)
            continue;

        const glm::vec3 center = (minP + maxP) * 0.5f;

        if (glm::length2(center) < 1e-12f)
            continue;

        // --------------------------------------------------
        // Apply translation
        // --------------------------------------------------
        for (int32_t v : verts)
        {
            if (!mesh->vert_valid(v))
                continue;

            const glm::vec3 p = mesh->vert_position(v);
            mesh->move_vert(v, p - center);
        }

        anyChanged = true;
    }

    return anyChanged;
}
