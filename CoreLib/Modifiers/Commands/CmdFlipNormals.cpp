#include "CmdFlipNormals.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    constexpr int kNormMapId = 0;

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
} // namespace

bool CmdFlipNormals::execute(Scene* scene)
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
        if (normMap < 0 || mesh->map_dim(normMap) != 3)
            continue;

        const std::vector<int32_t> polys = selected_or_all_polys(mesh);
        if (polys.empty())
            continue;

        for (int32_t pid : polys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            if (!mesh->map_poly_valid(normMap, pid))
                continue;

            const SysPolyVerts& mp = mesh->map_poly_verts(normMap, pid);
            if (mp.size() < 3)
                continue;

            // Rewrite the per-poly normal mapping so we NEVER accidentally flip
            // a shared normal-map vertex used by other polys.
            SysPolyVerts newMp = {};
            newMp.reserve(mp.size());

            bool ok = true;

            for (int32_t mv : mp)
            {
                const float* p = mesh->map_vert_position(normMap, mv);
                if (!p)
                {
                    ok = false;
                    break;
                }

                const float   flipped[3] = {-p[0], -p[1], -p[2]};
                const int32_t newMv      = mesh->map_create_vert(normMap, flipped);
                if (newMv < 0)
                {
                    ok = false;
                    break;
                }

                newMp.push_back(newMv);
            }

            if (!ok || newMp.size() != mp.size())
                continue;

            mesh->map_remove_poly(normMap, pid);
            mesh->map_create_poly(normMap, pid, newMp);

            any = true;
        }
    }

    return any;
}
