#include "CmdRestOnGround.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    static void collect_verts_from_polys(const SysMesh* mesh, const std::vector<int32_t>& polys, std::vector<int32_t>& out)
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

    static void collect_all_valid_verts(const SysMesh* mesh, std::vector<int32_t>& out)
    {
        out = mesh->all_verts();
        // all_verts() should already be valid indices; no further filtering needed.
    }
} // namespace

bool CmdRestOnGround::execute(Scene* scene)
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

        // -----------------------------
        // Choose working vert set
        // -----------------------------
        std::vector<int32_t> verts = {};

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
            collect_all_valid_verts(mesh, verts);
        }

        if (verts.empty())
            continue;

        // -----------------------------
        // Compute min Y
        // -----------------------------
        float minY = FLT_MAX;

        for (int32_t v : verts)
        {
            if (!mesh->vert_valid(v))
                continue;

            const glm::vec3& p = mesh->vert_position(v);
            minY               = std::min(minY, p.y);
        }

        if (minY == FLT_MAX)
            continue;

        // Ground plane is Y=0
        const float dy = -minY;

        if (std::abs(dy) < 1e-8f)
            continue;

        // -----------------------------
        // Apply translation
        // -----------------------------
        for (int32_t v : verts)
        {
            if (!mesh->vert_valid(v))
                continue;

            const glm::vec3 p = mesh->vert_position(v);
            mesh->move_vert(v, glm::vec3(p.x, p.y + dy, p.z));
        }

        anyChanged = true;
    }

    return anyChanged;
}
