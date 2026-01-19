#include "CmdFitView.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <unordered_set>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"
#include "Viewport.hpp"

// ------------------- helpers (local to this TU) -------------------

static bool meshHasAnySelection(const SysMesh* mesh)
{
    return mesh && (!mesh->selected_verts().empty() ||
                    !mesh->selected_edges().empty() ||
                    !mesh->selected_polys().empty());
}

static void expandMinMax(glm::vec3& mn, glm::vec3& mx, const glm::vec3& p)
{
    mn = glm::min(mn, p);
    mx = glm::max(mx, p);
}

static bool computeMeshSelectionAabb(const SysMesh* mesh, glm::vec3& outMin, glm::vec3& outMax)
{
    if (!mesh)
        return false;

    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(-std::numeric_limits<float>::max());

    bool gotAny = false;

    // 1) Selected verts
    if (!mesh->selected_verts().empty())
    {
        for (int32_t v : mesh->selected_verts())
        {
            if (!mesh->vert_valid(v))
                continue;
            expandMinMax(outMin, outMax, mesh->vert_position(v));
            gotAny = true;
        }
        return gotAny;
    }

    // 2) Selected edges -> their endpoints
    if (!mesh->selected_edges().empty())
    {
        for (const IndexPair& e : mesh->selected_edges())
        {
            if (mesh->vert_valid(e.first))
            {
                expandMinMax(outMin, outMax, mesh->vert_position(e.first));
                gotAny = true;
            }
            if (mesh->vert_valid(e.second))
            {
                expandMinMax(outMin, outMax, mesh->vert_position(e.second));
                gotAny = true;
            }
        }
        return gotAny;
    }

    // 3) Selected polys -> all verts of those polys
    if (!mesh->selected_polys().empty())
    {
        std::unordered_set<int32_t> uniq;
        uniq.reserve(mesh->selected_polys().size() * 4);

        for (int32_t p : mesh->selected_polys())
        {
            if (!mesh->poly_valid(p))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(p);
            for (int32_t v : pv)
                uniq.insert(v);
        }

        for (int32_t v : uniq)
        {
            if (!mesh->vert_valid(v))
                continue;
            expandMinMax(outMin, outMax, mesh->vert_position(v));
            gotAny = true;
        }
        return gotAny;
    }

    return false;
}

// Scene AABB:
// If any selection exists anywhere, bound the selection only.
// Otherwise bound all verts of visible meshes.
static bool computeSceneAabb(Scene* scene, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(-std::numeric_limits<float>::max());

    bool anySelectionInScene = false;
    for (auto* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        const SysMesh* mesh = sm->sysMesh();
        if (meshHasAnySelection(mesh))
        {
            anySelectionInScene = true;
            break;
        }
    }

    bool gotAny = false;

    for (auto* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        const SysMesh* mesh = sm->sysMesh();
        if (!mesh || mesh->num_verts() == 0)
            continue;

        if (anySelectionInScene)
        {
            glm::vec3 mn, mx;
            if (!computeMeshSelectionAabb(mesh, mn, mx))
                continue;

            expandMinMax(outMin, outMax, mn);
            expandMinMax(outMin, outMax, mx);
            gotAny = true;
        }
        else
        {
            for (int32_t v : mesh->all_verts())
            {
                if (!mesh->vert_valid(v))
                    continue;

                expandMinMax(outMin, outMax, mesh->vert_position(v));
                gotAny = true;
            }
        }
    }

    return gotAny;
}

static bool isGoodProjected(const glm::vec3& sp) noexcept
{
    if (!std::isfinite(sp.x) || !std::isfinite(sp.y) || !std::isfinite(sp.z))
        return false;

    // With RH_ZO, visible points should generally land in [0,1] in ndc.z.
    // Be tolerant (clipped points can be slightly outside).
    if (sp.z < -0.25f || sp.z > 1.25f)
        return false;

    return true;
}

// Project bbox corners and return screen-space bounds (pixels).
// static glm::vec4 projectAabbPixels(Viewport*        vp,
//                                    const glm::vec3& bmin,
//                                    const glm::vec3& bmax)
// {
//     const glm::vec3 c[8] = {
//         {bmin.x, bmin.y, bmin.z},
//         {bmax.x, bmin.y, bmin.z},
//         {bmin.x, bmax.y, bmin.z},
//         {bmax.x, bmax.y, bmin.z},
//         {bmin.x, bmin.y, bmax.z},
//         {bmax.x, bmin.y, bmax.z},
//         {bmin.x, bmax.y, bmax.z},
//         {bmax.x, bmax.y, bmax.z},
//     };

//     float minx = std::numeric_limits<float>::max();
//     float miny = std::numeric_limits<float>::max();
//     float maxx = -std::numeric_limits<float>::max();
//     float maxy = -std::numeric_limits<float>::max();

//     for (int i = 0; i < 8; ++i)
//     {
//         const glm::vec3 sp = vp->project(c[i]);
//         if (!std::isfinite(sp.x) || !std::isfinite(sp.y))
//             continue;

//         minx = std::min(minx, sp.x);
//         maxx = std::max(maxx, sp.x);
//         miny = std::min(miny, sp.y);
//         maxy = std::max(maxy, sp.y);
//     }

//     return glm::vec4(minx, miny, maxx, maxy);
// }

// ------------------- command -------------------

bool CmdFitView::execute(Scene* scene)
{
    if (!scene)
        return false;

    Viewport* vp = scene->activeViewport();
    if (!vp)
        return false;

    if (vp->width() <= 0 || vp->height() <= 0)
        return false;

    // Ensure matrices are current before any projection/unprojection.
    vp->apply();

    // 1) AABB (selection-aware)
    glm::vec3 bmin, bmax;
    if (!computeSceneAabb(scene, bmin, bmax))
        return false;

    const glm::vec3 centerW = 0.5f * (bmin + bmax);
    const float     viewCX  = vp->width() * 0.5f;
    const float     viewCY  = vp->height() * 0.5f;

    // 2) PAN: iterate a few times to center bbox center on screen.
    // NOTE: Viewport::pan uses pt.y -= deltaY internally, so pass -dy here.
    {
        constexpr int   MAX_IT = 8;
        constexpr float EPS    = 0.75f;

        for (int i = 0; i < MAX_IT; ++i)
        {
            const glm::vec3 sp = vp->project(centerW);
            if (!isGoodProjected(sp))
                break;

            const float dx = viewCX - sp.x;
            const float dy = viewCY - sp.y;

            if (std::fabs(dx) < EPS && std::fabs(dy) < EPS)
                break;

            vp->pan(dx, -dy);
            vp->apply();
        }
    }

    // 3) ZOOM: robust for huge models (calibrate with BOTH directions)
    {
        auto measure_box_pixels = [&](Viewport*        vp,
                                      const glm::vec3& bmin,
                                      const glm::vec3& bmax) -> float {
            const glm::vec3 c[8] = {
                {bmin.x, bmin.y, bmin.z},
                {bmax.x, bmin.y, bmin.z},
                {bmin.x, bmax.y, bmin.z},
                {bmax.x, bmax.y, bmin.z},
                {bmin.x, bmin.y, bmax.z},
                {bmax.x, bmin.y, bmax.z},
                {bmin.x, bmax.y, bmax.z},
                {bmax.x, bmax.y, bmax.z},
            };

            float minx = std::numeric_limits<float>::max();
            float miny = std::numeric_limits<float>::max();
            float maxx = -std::numeric_limits<float>::max();
            float maxy = -std::numeric_limits<float>::max();

            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 sp = vp->project(c[i]);
                if (!std::isfinite(sp.x) || !std::isfinite(sp.y))
                    continue;

                minx = std::min(minx, sp.x);
                maxx = std::max(maxx, sp.x);
                miny = std::min(miny, sp.y);
                maxy = std::max(maxy, sp.y);
            }

            return std::max(maxx - minx, maxy - miny);
        };

        constexpr float PAD    = 0.90f;
        constexpr float TOL    = 0.06f;
        constexpr int   MAX_IT = 36;

        const float target = std::min(vp->width() * PAD, vp->height() * PAD);

        auto measure_box = [&]() -> float {
            // use your local measure_box_pixels lambda (max of projected bbox width/height)
            const float s = measure_box_pixels(vp, bmin, bmax);
            return (std::isfinite(s) ? s : 0.0f);
        };

        float s0 = measure_box();
        if (s0 <= 0.0f)
            return false;

        // --- Probe BOTH directions and choose which one zooms IN (increases box size)
        constexpr float kProbe = 80.0f;

        vp->zoom(+kProbe, 0.0f);
        vp->apply();
        const float sPlus = measure_box();

        vp->zoom(-2.0f * kProbe, 0.0f); // net -kProbe from original
        vp->apply();
        const float sMinus = measure_box();

        // Return to baseline (back to original)
        vp->zoom(+kProbe, 0.0f);
        vp->apply();

        // If neither probe is meaningful, bail safely.
        if (sPlus <= 0.0f && sMinus <= 0.0f)
            return false;

        // Determine which direction increases projected size (zooms in)
        // If both are valid, pick the larger.
        const bool plusZoomsIn =
            (sPlus > s0 && sPlus >= sMinus) ||
            (sMinus <= s0 && sPlus > sMinus);

        // --- Converge
        float step    = 420.0f; // bigger initial step helps huge models
        float totalDx = 0.0f;

        for (int i = 0; i < MAX_IT; ++i)
        {
            const float s = measure_box();
            if (s <= 0.0f)
                break;

            const float ratio = s / target; // >1 too big, <1 too small
            if (std::fabs(ratio - 1.0f) <= TOL)
                break;

            const bool needZoomIn = (ratio < 1.0f);

            // Choose dx sign to move toward target
            float sign = 0.0f;
            if (needZoomIn)
                sign = plusZoomsIn ? +1.0f : -1.0f;
            else
                sign = plusZoomsIn ? -1.0f : +1.0f;

            const float gain = std::clamp(std::fabs(ratio - 1.0f), 0.15f, 3.0f);
            float       dx   = sign * step * gain;

            dx = std::clamp(dx, -1200.0f, 1200.0f);

            vp->zoom(dx, 0.0f);
            vp->apply();

            totalDx += dx;
            if (std::fabs(totalDx) > 60000.0f) // safety net for crazy imports
                break;

            step = std::max(60.0f, step * 0.82f);
        }
    }

    return true;
}
