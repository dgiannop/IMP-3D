#include "SelectionUtils.hpp"

#include <cmath>
#include <glm/gtx/compatibility.hpp>

#include "Scene.hpp"
#include "SysMesh.hpp"

namespace sel
{
    namespace
    {
        inline void push_unique_vert(std::vector<int32_t>& out,
                                     std::vector<uint8_t>& mark,
                                     const int32_t         vi) noexcept
        {
            if (vi < 0)
                return;

            const uint32_t uvi = static_cast<uint32_t>(vi);
            if (uvi >= mark.size())
                return;

            if (mark[uvi])
                return;

            mark[uvi] = 1;
            out.push_back(vi);
        }

        inline bool is_zero3(const glm::vec3& v, float eps = 1e-12f) noexcept
        {
            return (std::abs(v.x) <= eps) && (std::abs(v.y) <= eps) && (std::abs(v.z) <= eps);
        }

        inline glm::vec3 safe_normalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
        {
            const float len2 = glm::dot(v, v);
            if (len2 <= 1e-12f)
                return fallback;

            return v * (1.0f / std::sqrt(len2));
        }

        inline float dist2(const glm::vec3& a, const glm::vec3& b) noexcept
        {
            const glm::vec3 d = a - b;
            return glm::dot(d, d);
        }
    } // namespace

    MeshVertMap to_verts(Scene* scene)
    {
        MeshVertMap result = {};

        if (!scene)
            return result;

        const SelectionMode mode = scene->selectionMode();

        // ------------------------------------------------------------
        // 1) Scene-wide: does ANY active mesh have a selection?
        // ------------------------------------------------------------
        bool anySelected = false;

        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            switch (mode)
            {
                case SelectionMode::VERTS:
                    anySelected = !mesh->selected_verts().empty();
                    break;

                case SelectionMode::EDGES:
                    anySelected = !mesh->selected_edges().empty();
                    break;

                case SelectionMode::POLYS:
                    anySelected = !mesh->selected_polys().empty();
                    break;
            }

            if (anySelected)
                break;
        }

        // ------------------------------------------------------------
        // 2) Build per-mesh vert lists (selected-only if anySelected)
        // ------------------------------------------------------------
        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            std::vector<int32_t> verts = {};
            std::vector<uint8_t> mark  = {};
            mark.resize(mesh->vert_buffer_size(), 0);

            // ------------------------------------------------------------
            // VERTS mode
            // ------------------------------------------------------------
            if (mode == SelectionMode::VERTS)
            {
                const std::vector<int32_t>& selv = mesh->selected_verts();

                if (anySelected)
                {
                    if (selv.empty())
                        continue;

                    verts.reserve(selv.size());
                    for (int32_t vi : selv)
                        push_unique_vert(verts, mark, vi);
                }
                else
                {
                    const std::vector<int32_t>& allv = mesh->all_verts();
                    verts.reserve(allv.size());
                    for (int32_t vi : allv)
                        push_unique_vert(verts, mark, vi);
                }

                if (!verts.empty())
                    result[mesh] = std::move(verts);

                continue;
            }

            // ------------------------------------------------------------
            // EDGES mode
            // ------------------------------------------------------------
            if (mode == SelectionMode::EDGES)
            {
                const std::vector<IndexPair>& sele = mesh->selected_edges();

                if (anySelected)
                {
                    if (sele.empty())
                        continue;

                    verts.reserve(sele.size() * 2);
                    for (const IndexPair& e : sele)
                    {
                        push_unique_vert(verts, mark, e.first);
                        push_unique_vert(verts, mark, e.second);
                    }
                }
                else
                {
                    const std::vector<IndexPair> alle = mesh->all_edges(); // by value
                    verts.reserve(alle.size() * 2);
                    for (const IndexPair& e : alle)
                    {
                        push_unique_vert(verts, mark, e.first);
                        push_unique_vert(verts, mark, e.second);
                    }
                }

                if (!verts.empty())
                    result[mesh] = std::move(verts);

                continue;
            }

            // ------------------------------------------------------------
            // POLYS mode
            // ------------------------------------------------------------
            if (mode == SelectionMode::POLYS)
            {
                const std::vector<int32_t>& selp = mesh->selected_polys();

                if (anySelected)
                {
                    if (selp.empty())
                        continue;

                    verts.reserve(selp.size() * 4);

                    for (int32_t pi : selp)
                    {
                        if (!mesh->poly_valid(pi))
                            continue;

                        const SysPolyVerts& pv = mesh->poly_verts(pi);
                        for (int32_t vi : pv)
                            push_unique_vert(verts, mark, vi);
                    }
                }
                else
                {
                    const std::vector<int32_t>& allp = mesh->all_polys();
                    verts.reserve(allp.size() * 4);

                    for (int32_t pi : allp)
                    {
                        if (!mesh->poly_valid(pi))
                            continue;

                        const SysPolyVerts& pv = mesh->poly_verts(pi);
                        for (int32_t vi : pv)
                            push_unique_vert(verts, mark, vi);
                    }
                }

                if (!verts.empty())
                    result[mesh] = std::move(verts);

                continue;
            }
        }

        return result;
    }

    MeshEdgeMap to_edges(Scene* scene)
    {
        MeshEdgeMap result = {};
        if (!scene)
            return result;

        if (scene->selectionMode() != SelectionMode::EDGES)
            return result;

        // Scene-wide: any selection?
        bool anySelected = false;
        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            if (!mesh->selected_edges().empty())
            {
                anySelected = true;
                break;
            }
        }

        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            const auto& sel = mesh->selected_edges();

            if (anySelected)
            {
                if (sel.empty())
                    continue;

                result[mesh] = sel;
            }
            else
            {
                result[mesh] = mesh->all_edges(); // by value
            }
        }

        return result;
    }

    MeshPolyMap to_polys(Scene* scene)
    {
        MeshPolyMap result = {};
        if (!scene)
            return result;

        if (scene->selectionMode() != SelectionMode::POLYS)
            return result;

        // Scene-wide: any selection?
        bool anySelected = false;
        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            if (!mesh->selected_polys().empty())
            {
                anySelected = true;
                break;
            }
        }

        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            const auto& sel = mesh->selected_polys();

            if (anySelected)
            {
                if (sel.empty())
                    continue;

                result[mesh] = sel;
            }
            else
            {
                result[mesh] = mesh->all_polys();
            }
        }

        return result;
    }

    bool has_selection(Scene* scene) noexcept
    {
        if (!scene)
            return false;

        const SelectionMode mode = scene->selectionMode();

        for (SysMesh* mesh : scene->activeMeshes())
        {
            if (!mesh)
                continue;

            switch (mode)
            {
                case SelectionMode::VERTS:
                    if (!mesh->selected_verts().empty())
                        return true;
                    break;

                case SelectionMode::EDGES:
                    if (!mesh->selected_edges().empty())
                        return true;
                    break;

                case SelectionMode::POLYS:
                    if (!mesh->selected_polys().empty())
                        return true;
                    break;
            }
        }

        return false;
    }

    Aabb selection_bounds(Scene* scene) noexcept
    {
        Aabb b = {};

        if (!scene)
            return b;

        const MeshVertMap mv = to_verts(scene);

        for (const auto& [mesh, verts] : mv)
        {
            if (!mesh)
                continue;

            for (int32_t vi : verts)
            {
                if (!mesh->vert_valid(vi))
                    continue;

                const glm::vec3 p = mesh->vert_position(vi);

                // IMPORTANT: skip NaN/Inf verts (import bugs / bad data)
                if (!glm::all(glm::isfinite(p)))
                    continue;

                if (!b.valid)
                {
                    b.min   = p;
                    b.max   = p;
                    b.valid = true;
                }
                else
                {
                    b.min = glm::min(b.min, p);
                    b.max = glm::max(b.max, p);
                }
            }
        }

        return b;
    }

    glm::vec3 selection_center_mean(Scene* scene) noexcept
    {
        if (!scene)
            return glm::vec3{0.0f};

        const MeshVertMap mv = to_verts(scene);

        double   sx = 0.0;
        double   sy = 0.0;
        double   sz = 0.0;
        uint64_t n  = 0;

        for (const auto& [mesh, verts] : mv)
        {
            if (!mesh)
                continue;

            for (int32_t vi : verts)
            {
                if (!mesh->vert_valid(vi))
                    continue;

                const glm::vec3& p = mesh->vert_position(vi);
                sx += static_cast<double>(p.x);
                sy += static_cast<double>(p.y);
                sz += static_cast<double>(p.z);
                ++n;
            }
        }

        if (n == 0)
            return glm::vec3{0.0f};

        const double inv = 1.0 / static_cast<double>(n);
        return glm::vec3{
            static_cast<float>(sx * inv),
            static_cast<float>(sy * inv),
            static_cast<float>(sz * inv)};
    }

    glm::vec3 selection_center_bounds(Scene* scene) noexcept
    {
        const Aabb b = selection_bounds(scene);
        if (!b.valid)
            return glm::vec3{0.0f};

        return (b.min + b.max) * 0.5f;
    }

    glm::vec3 selection_center(Scene* scene) noexcept
    {
        // Default pivot: bounds center (gizmo-friendly).
        return selection_center_bounds(scene);
    }

    glm::vec3 selection_normal(Scene* scene) noexcept
    {
        if (!scene)
            return glm::vec3{0.0f, 0.0f, 1.0f};

        const SelectionMode mode = scene->selectionMode();

        // ------------------------------------------------------------
        // POLYS mode: average poly normals directly (best-defined).
        // Uses to_polys(scene) so it respects scene-wide selection rules.
        // ------------------------------------------------------------
        if (mode == SelectionMode::POLYS)
        {
            const MeshPolyMap mp = to_polys(scene);

            glm::vec3 sum = {0.0f, 0.0f, 0.0f};
            uint64_t  n   = 0;

            for (const auto& [mesh, polys] : mp)
            {
                if (!mesh)
                    continue;

                for (int32_t pi : polys)
                {
                    if (!mesh->poly_valid(pi))
                        continue;

                    const glm::vec3 pn = mesh->poly_normal(pi);
                    if (is_zero3(pn))
                        continue;

                    sum += pn;
                    ++n;
                }
            }

            return safe_normalize(sum, glm::vec3{0.0f, 0.0f, 1.0f});
        }

        // ------------------------------------------------------------
        // VERTS/EDGES mode: estimate by averaging normals of incident polys.
        // Uses to_verts(scene) so it respects scene-wide selection rules.
        // ------------------------------------------------------------
        const MeshVertMap mv = to_verts(scene);

        glm::vec3 sum = {0.0f, 0.0f, 0.0f};
        uint64_t  n   = 0;

        for (const auto& [mesh, verts] : mv)
        {
            if (!mesh)
                continue;

            for (int32_t vi : verts)
            {
                if (!mesh->vert_valid(vi))
                    continue;

                const SysVertPolys& vp = mesh->vert_polys(vi);
                for (int32_t pi : vp)
                {
                    if (!mesh->poly_valid(pi))
                        continue;

                    const glm::vec3 pn = mesh->poly_normal(pi);
                    if (is_zero3(pn))
                        continue;

                    sum += pn;
                    ++n;
                }
            }
        }

        return safe_normalize(sum, glm::vec3{0.0f, 0.0f, 1.0f});
    }

    bool selection_surface_anchor(Scene* scene, glm::vec3& outPos, glm::vec3& outN) noexcept
    {
        outPos = glm::vec3{0.0f};
        outN   = glm::vec3{0.0f, 0.0f, 1.0f};

        if (!scene)
            return false;

        const glm::vec3 pivot = selection_center_bounds(scene);

        // Try to anchor to a real polygon whose center is closest to the pivot.
        bool     found    = false;
        float    bestD2   = 0.0f;
        SysMesh* bestMesh = nullptr;
        int32_t  bestPoly = -1;

        const SelectionMode mode = scene->selectionMode();

        if (mode == SelectionMode::POLYS)
        {
            // Uses to_polys(scene) so it respects scene-wide selection rules.
            const MeshPolyMap mp = to_polys(scene);

            for (const auto& [mesh, polys] : mp)
            {
                if (!mesh)
                    continue;

                for (int32_t pi : polys)
                {
                    if (!mesh->poly_valid(pi))
                        continue;

                    const glm::vec3 c = mesh->poly_center(pi);
                    const float     d = dist2(c, pivot);

                    if (!found || d < bestD2)
                    {
                        found    = true;
                        bestD2   = d;
                        bestMesh = mesh;
                        bestPoly = pi;
                    }
                }
            }
        }
        else
        {
            // Use incident polys of the vertex set from to_verts(scene).
            const MeshVertMap mv = to_verts(scene);

            for (const auto& [mesh, verts] : mv)
            {
                if (!mesh)
                    continue;

                for (int32_t vi : verts)
                {
                    if (!mesh->vert_valid(vi))
                        continue;

                    const SysVertPolys& vpolys = mesh->vert_polys(vi);
                    for (int32_t pi : vpolys)
                    {
                        if (!mesh->poly_valid(pi))
                            continue;

                        const glm::vec3 c = mesh->poly_center(pi);
                        const float     d = dist2(c, pivot);

                        if (!found || d < bestD2)
                        {
                            found    = true;
                            bestD2   = d;
                            bestMesh = mesh;
                            bestPoly = pi;
                        }
                    }
                }
            }
        }

        if (found && bestMesh && bestPoly >= 0)
        {
            outPos = bestMesh->poly_center(bestPoly);
            outN   = safe_normalize(bestMesh->poly_normal(bestPoly), glm::vec3{0.0f, 0.0f, 1.0f});
            return true;
        }

        // Fallback: no polygons discovered (e.g., empty meshes)
        outPos = pivot;
        outN   = selection_normal(scene);
        return false;
    }

    float selection_radius(Scene* scene) noexcept
    {
        const Aabb b = selection_bounds(scene);
        if (!b.valid)
            return 0.0f;

        const glm::vec3 d = b.max - b.min;
        return 0.5f * std::sqrt(glm::dot(d, d));
    }

} // namespace sel
