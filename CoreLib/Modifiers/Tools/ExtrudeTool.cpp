//=============================================================================
// ExtrudeTool.cpp
//=============================================================================
#include "ExtrudeTool.hpp"

#include <SysMesh.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CoreUtilities.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

// -----------------------------------------------------------------------------
// Toggle
// -----------------------------------------------------------------------------
// If true, use NormalPullGizmo.
// If false, drag anywhere extrudes by accumulating mouse delta in pixels.
static constexpr bool kUseExtrudeGizmo = false;

ExtrudeTool::ExtrudeTool()
{
    addProperty("Amount", PropertyType::FLOAT, &m_amount, 0.f);
    addProperty("Group polygons", PropertyType::BOOL, &m_group);
}

void ExtrudeTool::activate(Scene* /*scene*/)
{
}

void ExtrudeTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_amount))
        return;

    // For now, drive polys. Expand this later:
    //  - VERTS mode -> extrudeVerts
    //  - EDGES mode -> extrudeEdges
    auto polyMap = sel::to_polys(scene);

    for (auto& [mesh, polys] : polyMap)
    {
        if (!mesh)
            continue;

        ExtrudeTool::extrudePolys(mesh, polys, m_amount, m_group);
    }
}

void ExtrudeTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    // Reset preview delta at interaction start.
    m_amount = 0.0f;

    if constexpr (kUseExtrudeGizmo)
    {
        m_gizmo.mouseDown(vp, scene, event);
    }

    propertiesChanged(scene);
}

void ExtrudeTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    if constexpr (kUseExtrudeGizmo)
    {
        m_gizmo.mouseDrag(vp, scene, event);
    }
    else
    {
        // Drag anywhere (no gizmo). Simple accumulation in "world units".
        const float s = vp->pixelScale();
        m_amount += static_cast<float>(event.deltaX) * s;
        m_amount += static_cast<float>(event.deltaY) * s;
    }

    propertiesChanged(scene);
}

void ExtrudeTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    if constexpr (kUseExtrudeGizmo)
    {
        m_gizmo.mouseUp(vp, scene, event);
    }

    scene->commitMeshChanges();

    // Reset for next interaction.
    m_amount = 0.0f;
}

void ExtrudeTool::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    if constexpr (kUseExtrudeGizmo)
    {
        m_gizmo.render(vp, scene);
    }
}

OverlayHandler* ExtrudeTool::overlayHandler()
{
    if constexpr (kUseExtrudeGizmo)
    {
        return &m_gizmo.overlayHandler();
    }
    else
    {
        return nullptr;
    }
}

// ------------------------------------------------------------
// Static operations
// ------------------------------------------------------------
void ExtrudeTool::extrudePolys(SysMesh* mesh, std::span<const int32_t> polys, float amount, bool group)
{
    if (!mesh || polys.empty() || un::is_zero(amount))
        return;

    auto appendPoly = [](SysPolyVerts& dst, int32_t v) noexcept { dst.push_back(v); };

    // Track caps we create so we can select them afterwards.
    std::vector<int32_t> newCaps = {};
    newCaps.reserve(polys.size());

    // Track originals to delete (filter invalid, and avoid duplicates).
    std::vector<int32_t> oldPolys = {};
    oldPolys.reserve(polys.size());

    for (int32_t pi : polys)
    {
        if (mesh->poly_valid(pi))
            oldPolys.push_back(pi);
    }

    if (oldPolys.empty())
        return;

    // ------------------------------------------------------------
    // group == false: per-face extrusion
    // ------------------------------------------------------------
    if (!group)
    {
        SysPolyVerts top  = {};
        SysPolyVerts quad = {};

        for (int32_t pi : oldPolys)
        {
            const SysPolyVerts& pv = mesh->poly_verts(pi);
            if (pv.size() < 3)
                continue;

            const glm::vec3 n = un::safe_normalize(mesh->poly_normal(pi));
            if (un::is_zero(n))
                continue;

            const uint32_t mat = mesh->poly_material(pi);

            // Duplicate verts for this poly only (no sharing).
            top.clear();
            for (int32_t vi : pv)
            {
                const glm::vec3 p = mesh->vert_position(vi);
                appendPoly(top, mesh->create_vert(p + n * amount));
            }

            // Cap (top face)
            const int32_t capPi = mesh->create_poly(top, mat);
            newCaps.push_back(capPi);

            // Side walls for every edge
            const size_t nverts = pv.size();
            for (size_t i = 0; i < nverts; ++i)
            {
                const int32_t a  = pv[i];
                const int32_t b  = pv[(i + 1) % nverts];
                const int32_t ap = top[i];
                const int32_t bp = top[(i + 1) % nverts];

                quad.clear();
                appendPoly(quad, a);
                appendPoly(quad, b);
                appendPoly(quad, bp);
                appendPoly(quad, ap);

                mesh->create_poly(quad, mat);
            }
        }

        // Delete originals
        for (int32_t pi : oldPolys)
            mesh->remove_poly(pi);

        // Select caps
        mesh->clear_selected_polys();
        for (int32_t pi : newCaps)
            mesh->select_poly(pi, true);

        return;
    }

    // ------------------------------------------------------------
    // group == true: connected region extrusion
    // ------------------------------------------------------------

    // Selected poly set
    std::unordered_set<int32_t> selPolys = {};
    selPolys.reserve(oldPolys.size() * 2);
    for (int32_t pi : oldPolys)
        selPolys.insert(pi);

    // Per-vertex accumulated normal (sum of poly normals)
    std::unordered_map<int32_t, glm::vec3> vNormalSum = {};
    vNormalSum.reserve(selPolys.size() * 8);

    struct EdgeInfo
    {
        int32_t  count    = 0;
        int32_t  a        = -1; // directed as seen in owning selected poly
        int32_t  b        = -1;
        uint32_t material = 0; // owning poly material for side wall
    };

    auto packEdge = [](int32_t a, int32_t b) noexcept -> uint64_t {
        uint32_t ua = static_cast<uint32_t>(a);
        uint32_t ub = static_cast<uint32_t>(b);
        if (ua > ub)
            std::swap(ua, ub);
        return (uint64_t(ua) << 32) | uint64_t(ub);
    };

    std::unordered_map<uint64_t, EdgeInfo> edgeInfo = {};
    edgeInfo.reserve(selPolys.size() * 8);

    // Pass 1: gather normals + edge counts
    for (int32_t pi : oldPolys)
    {
        const SysPolyVerts& pv = mesh->poly_verts(pi);
        if (pv.size() < 3)
            continue;

        const glm::vec3 pn  = un::safe_normalize(mesh->poly_normal(pi));
        const uint32_t  mat = mesh->poly_material(pi);

        for (int32_t vi : pv)
            vNormalSum[vi] += pn;

        const size_t nverts = pv.size();
        for (size_t i = 0; i < nverts; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) % nverts];

            const uint64_t key = packEdge(a, b);
            auto&          e   = edgeInfo[key];
            e.count++;

            if (e.count == 1)
            {
                e.a        = a;
                e.b        = b;
                e.material = mat;
            }
        }
    }

    // Pass 2: duplicate verts shared across region
    std::unordered_map<int32_t, int32_t> vDup = {};
    vDup.reserve(vNormalSum.size() * 2);

    for (auto& [vi, nsum] : vNormalSum)
    {
        glm::vec3 n = un::safe_normalize(nsum);
        if (un::is_zero(n))
            n = glm::vec3(0.0f, 0.0f, 1.0f);

        const glm::vec3 p  = mesh->vert_position(vi);
        const int32_t   v2 = mesh->create_vert(p + n * amount);
        vDup.emplace(vi, v2);
    }

    // Pass 3: caps
    SysPolyVerts top = {};
    for (int32_t pi : oldPolys)
    {
        const SysPolyVerts& pv  = mesh->poly_verts(pi);
        const uint32_t      mat = mesh->poly_material(pi);

        if (pv.size() < 3)
            continue;

        top.clear();
        for (int32_t vi : pv)
            appendPoly(top, vDup.at(vi)); // guaranteed by construction

        const int32_t capPi = mesh->create_poly(top, mat);
        newCaps.push_back(capPi);
    }

    // Pass 4: boundary walls only
    SysPolyVerts quad = {};
    for (const auto& [key, e] : edgeInfo)
    {
        if (e.count != 1)
            continue;

        const int32_t a  = e.a;
        const int32_t b  = e.b;
        const int32_t ap = vDup.at(a);
        const int32_t bp = vDup.at(b);

        quad.clear();
        appendPoly(quad, a);
        appendPoly(quad, b);
        appendPoly(quad, bp);
        appendPoly(quad, ap);

        mesh->create_poly(quad, e.material);
    }

    // Delete originals
    for (int32_t pi : oldPolys)
        mesh->remove_poly(pi);

    // Select caps
    mesh->clear_selected_polys();
    for (int32_t pi : newCaps)
        mesh->select_poly(pi, true);
}

void ExtrudeTool::extrudeVerts(SysMesh* /*mesh*/, std::span<const int32_t> /*verts*/, float /*amount*/, bool /*group*/)
{
    // Stub
}

void ExtrudeTool::extrudeEdges(SysMesh* /*mesh*/, std::span<const IndexPair> /*edges*/, float /*amount*/)
{
    // Stub
}
