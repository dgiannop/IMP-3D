#include "CmdCreatePoly.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

#include "CoreUtilities.hpp" // un::safe_normalize, un::is_zero (if you have them)
#include "Scene.hpp"
#include "SysMesh.hpp"

namespace
{
    static glm::vec3 compute_center(SysMesh* mesh, const std::vector<int32_t>& verts) noexcept
    {
        glm::vec3 c = {};
        int32_t   n = 0;

        for (int32_t vi : verts)
        {
            if (!mesh->vert_valid(vi))
                continue;

            c += mesh->vert_position(vi);
            ++n;
        }

        if (n > 0)
            c /= float(n);

        return c;
    }

    static void build_basis_from_normal(const glm::vec3& n, glm::vec3& t, glm::vec3& b) noexcept
    {
        // Pick helper axis least aligned with n (more stable than checking only n.z).
        const glm::vec3 an = glm::abs(n);
        const glm::vec3 h =
            (an.x <= an.y && an.x <= an.z) ? glm::vec3(1.0f, 0.0f, 0.0f) : (an.y <= an.x && an.y <= an.z) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                                                                          : glm::vec3(0.0f, 0.0f, 1.0f);

        t = un::safe_normalize(glm::cross(h, n));
        b = un::safe_normalize(glm::cross(n, t));
    }

    static glm::vec3 newell_normal(const SysPolyVerts& verts, SysMesh* mesh) noexcept
    {
        glm::vec3 n = {};
        if (verts.size() < 3)
            return n;

        for (int i = 0; i < verts.size(); ++i)
        {
            const int32_t va = verts[i];
            const int32_t vb = verts[(i + 1) % verts.size()];

            if (!mesh->vert_valid(va) || !mesh->vert_valid(vb))
                continue;

            const glm::vec3 a = mesh->vert_position(va);
            const glm::vec3 b = mesh->vert_position(vb);

            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }

        return un::safe_normalize(n);
    }

    struct AngleItem
    {
        int32_t vi = -1;
        float   a  = 0.0f;
        float   r2 = 0.0f;
    };

    static void radial_sort(SysPolyVerts&    verts,
                            SysMesh*         mesh,
                            const glm::vec3& center,
                            const glm::vec3& normal) noexcept
    {
        glm::vec3 t = {}, b = {};
        build_basis_from_normal(normal, t, b);

        std::vector<AngleItem> items;
        items.reserve(size_t(verts.size()));

        for (int32_t vi : verts)
        {
            const glm::vec3 p = mesh->vert_position(vi) - center;

            const float x  = glm::dot(p, t);
            const float y  = glm::dot(p, b);
            const float a  = std::atan2(y, x);
            const float r2 = x * x + y * y;

            items.push_back({vi, a, r2});
        }

        std::sort(items.begin(), items.end(), [](const AngleItem& A, const AngleItem& B) noexcept {
            if (A.a < B.a)
                return true;
            if (A.a > B.a)
                return false;
            return A.r2 > B.r2; // stable-ish tie break
        });

        for (int i = 0; i < verts.size(); ++i)
            verts[i] = items[size_t(i)].vi;
    }

    static glm::vec3 compute_expected_normal(SysMesh* mesh, const std::vector<int32_t>& vertsUnique) noexcept
    {
        glm::vec3 n = {};

        for (int32_t vi : vertsUnique)
        {
            const SysVertPolys& polys = mesh->vert_polys(vi);
            for (int32_t pi : polys)
                n += mesh->poly_normal(pi);
        }

        // Fallback: if no adjacent polys contributed (isolated verts), use first 3 verts.
        if (glm::dot(n, n) < 1e-10f && vertsUnique.size() >= 3)
        {
            const glm::vec3 a = mesh->vert_position(vertsUnique[0]);
            const glm::vec3 b = mesh->vert_position(vertsUnique[1]);
            const glm::vec3 c = mesh->vert_position(vertsUnique[2]);
            n                 = glm::cross(b - a, c - a);
        }

        return un::safe_normalize(n);
    }

    static int32_t pick_best_source_poly(SysMesh* mesh, int32_t vi, const glm::vec3& expectedN) noexcept
    {
        int32_t bestPi  = -1;
        float   bestDot = -1.0f;

        const SysVertPolys& polys = mesh->vert_polys(vi);
        for (int32_t pi : polys)
        {
            const glm::vec3 pn = mesh->poly_normal(pi);
            const float     d  = glm::dot(pn, expectedN);
            if (d > bestDot)
            {
                bestDot = d;
                bestPi  = pi;
            }
        }

        return bestPi;
    }

    static void try_copy_map_loops(SysMesh*            mesh,
                                   int32_t             mapId,
                                   int32_t             newPoly,
                                   const SysPolyVerts& sorted,
                                   const glm::vec3&    expectedN) noexcept
    {
        SysPolyVerts mapVerts = {};

        for (int32_t vi : sorted)
        {
            int32_t srcPi = pick_best_source_poly(mesh, vi, expectedN);
            if (srcPi < 0)
                return;

            if (!mesh->map_poly_valid(mapId, srcPi))
                return;

            const SysPolyVerts& base  = mesh->poly_verts(srcPi);
            const SysPolyVerts& mapPV = mesh->map_poly_verts(mapId, srcPi);

            bool found = false;
            for (int i = 0; i < base.size(); ++i)
            {
                if (base[i] == vi)
                {
                    mapVerts.push_back(mapPV[i]);
                    found = true;
                    break;
                }
            }

            if (!found)
                return;
        }

        if (mapVerts.size() == sorted.size())
            mesh->map_create_poly(mapId, newPoly, mapVerts);
    }
} // namespace

bool CmdCreatePoly::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool created = false;

    for (SysMesh* mesh : scene->activeMeshes())
    {
        if (!mesh)
            continue;

        const auto& sel = mesh->selected_verts();
        if (sel.size() < 3)
            continue;

        // ------------------------------------------------------------
        // 1) Deduplicate + validate selection
        // ------------------------------------------------------------
        std::unordered_set<int32_t> seen;
        seen.reserve(sel.size() * 2u);

        std::vector<int32_t> unique;
        unique.reserve(sel.size());

        for (int32_t vi : sel)
        {
            if (!mesh->vert_valid(vi))
                continue;

            if (seen.insert(vi).second)
                unique.push_back(vi);
        }

        if (unique.size() < 3)
            continue;

        // ------------------------------------------------------------
        // 2) Center + expected normal (from surrounding surface)
        // ------------------------------------------------------------
        const glm::vec3 center    = compute_center(mesh, unique);
        const glm::vec3 expectedN = compute_expected_normal(mesh, unique);

        if (glm::dot(expectedN, expectedN) < 1e-10f)
            continue; // can't form a stable plane

        // ------------------------------------------------------------
        // 3) Build SysPolyVerts and radial sort
        // ------------------------------------------------------------
        SysPolyVerts sorted = {};
        for (int32_t vi : unique)
            sorted.push_back(vi);

        radial_sort(sorted, mesh, center, expectedN);

        // ------------------------------------------------------------
        // 4) Fix winding using robust polygon normal (Newell)
        // ------------------------------------------------------------
        const glm::vec3 polyN = newell_normal(sorted, mesh);
        if (glm::dot(polyN, expectedN) < 0.0f)
            std::reverse(sorted.begin(), sorted.end());

        if (sorted.size() < 3)
            continue;

        // ------------------------------------------------------------
        // 5) Create poly
        // ------------------------------------------------------------
        const int32_t newPoly = mesh->create_poly(sorted);
        if (newPoly < 0)
            continue;

        created = true;

        // ------------------------------------------------------------
        // 6) Best-effort map propagation (0 normals, 1 uvs)
        // ------------------------------------------------------------
        try_copy_map_loops(mesh, 0, newPoly, sorted, expectedN);
        try_copy_map_loops(mesh, 1, newPoly, sorted, expectedN);

        // ------------------------------------------------------------
        // 7) Optional UX: selection changes (adjust to your real API)
        // ------------------------------------------------------------
        // mesh->clear_selected_verts();
        // mesh->clear_selected_edges();
        // mesh->clear_selected_polys();
        // mesh->select_poly(newPoly, true);
    }

    return created;
}
