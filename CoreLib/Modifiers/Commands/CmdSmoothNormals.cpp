#include "CmdSmoothNormals.hpp"

#include <algorithm>
#include <cstdint>
#include <glm/gtx/norm.hpp>
#include <unordered_set>
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

bool CmdSmoothNormals::execute(Scene* scene)
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

        // Build the set of vertices involved, and also a set of "faces to consider"
        // for smoothing accumulation (we use ALL incident faces for those verts,
        // so smoothing behaves naturally at selection boundaries).
        std::unordered_set<int32_t> touchedVerts;
        touchedVerts.reserve(polys.size() * 4ull);

        for (int32_t pid : polys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(pid);
            for (int32_t vi : pv)
                if (mesh->vert_valid(vi))
                    touchedVerts.insert(vi);
        }

        if (touchedVerts.empty())
            continue;

        // Accumulate smooth normals per vertex: sum of incident face normals.
        // We only compute for touched verts.
        std::vector<glm::vec3> vnorm(mesh->vert_buffer_size(), glm::vec3{0.0f});

        for (int32_t vi : touchedVerts)
        {
            const SysVertPolys& incident = mesh->vert_polys(vi);
            for (int32_t pid : incident)
            {
                if (!mesh->poly_valid(pid))
                    continue;

                // Area-weighted face normal (poly_normal should be fine for n-gons).
                const glm::vec3 fn = mesh->poly_normal(pid);
                vnorm[(size_t)vi] += fn;
            }
        }

        for (int32_t vi : touchedVerts)
            vnorm[(size_t)vi] = safe_normalize(vnorm[(size_t)vi]);

        // Write face-varying normals for the target polys
        for (int32_t pid : polys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(pid);
            if (pv.size() < 3)
                continue;

            SysPolyVerts nPoly = {};
            nPoly.reserve(pv.size());

            bool ok = true;
            for (int32_t vi : pv)
            {
                if (!mesh->vert_valid(vi))
                {
                    ok = false;
                    break;
                }

                const glm::vec3 nn     = vnorm[(size_t)vi];
                const float     tmp[3] = {nn.x, nn.y, nn.z};

                // Face-varying: new map vert per corner
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
