#include "CmdFlattenNormals.hpp"

#include <algorithm>
#include <cstdint>
#include <glm/gtx/norm.hpp>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    constexpr int kNormMapId = 0;

    static inline glm::vec3 safe_normalize(glm::vec3 v) noexcept
    {
        const float l2 = glm::length2(v);
        return (l2 > 1e-20f) ? v * glm::inversesqrt(l2) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

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

bool CmdFlattenNormals::execute(Scene* scene)
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

        // Ensure normal map exists and is dim=3
        int normMap = mesh->map_find(kNormMapId);
        if (normMap == -1)
            normMap = mesh->map_create(kNormMapId, /*type*/ 0, /*dim*/ 3);

        if (normMap < 0 || mesh->map_dim(normMap) != 3)
            continue;

        const std::vector<int32_t> polys = selected_or_all_polys(mesh);
        if (polys.empty())
            continue;

        for (int32_t pid : polys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(pid);
            if (pv.size() < 3)
                continue;

            // Flat normal for this poly
            const glm::vec3 fn     = safe_normalize(mesh->poly_normal(pid));
            const float     tmp[3] = {fn.x, fn.y, fn.z};

            SysPolyVerts nPoly = {};
            nPoly.reserve(pv.size());

            bool ok = true;
            for (size_t i = 0; i < pv.size(); ++i)
            {
                (void)i;

                // Face-varying: new map vert per corner (even though identical)
                const int32_t mv = mesh->map_create_vert(normMap, tmp);
                if (mv < 0)
                {
                    ok = false;
                    break;
                }
                nPoly.push_back(mv);
            }

            if (!ok || nPoly.size() != pv.size())
                continue;

            if (mesh->map_poly_valid(normMap, pid))
                mesh->map_remove_poly(normMap, pid);

            mesh->map_create_poly(normMap, pid, nPoly);
            any = true;
        }
    }

    return any;
}
