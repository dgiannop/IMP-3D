#include "DuplicateTool.hpp"

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <unordered_map>
#include <vector>

#include "CoreUtilities.hpp" // for un::ray (or wherever un::ray lives)
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"
#include "Viewport.hpp"

static inline glm::vec3 safe_normalize(glm::vec3 v) noexcept
{
    const float l2 = glm::length2(v);
    return (l2 > 1e-20f) ? v * glm::inversesqrt(l2) : glm::vec3(0.0f, 1.0f, 0.0f);
}

static inline bool rayPlaneIntersect(const un::ray&   r,
                                     const glm::vec3& planeOrg,
                                     const glm::vec3& planeNrm,
                                     glm::vec3&       outHit) noexcept
{
    const float denom = glm::dot(planeNrm, r.dir);
    if (std::abs(denom) < 1e-8f)
        return false;

    const float t = glm::dot(planeOrg - r.org, planeNrm) / denom;
    if (t < 0.0f)
        return false;

    outHit = r.org + r.dir * t;
    return true;
}

DuplicateTool::DuplicateTool()
{
}

void DuplicateTool::activate(Scene* scene)
{
    if (!scene)
        return;

    // Start a new pending batch and build duplicated geometry immediately.
    // (The Scene probably already begins a pending state automatically when tools modify;
    // if not, we can call scene->beginMeshChanges() here.)
    beginDuplicate(scene);
    m_active = !m_sets.empty();
}

void DuplicateTool::propertiesChanged(Scene* /*scene*/)
{
    // No properties for now.
}

// Qt-style bitmask:
constexpr int kLeft  = 1; // Qt::LeftButton
constexpr int kRight = 2; // Qt::RightButton

void DuplicateTool::mouseDown(Viewport* /*vp*/, Scene* scene, const CoreEvent& event)
{
    if (!scene || !m_active)
        return;

    if (event.button & kLeft)
    {
        scene->commitMeshChanges();
        m_active = false;
        m_sets.clear();
        return;
    }

    if (event.button & kRight)
    {
        scene->abortMeshChanges();
        m_active = false;
        m_sets.clear();
        return;
    }
}

void DuplicateTool::mouseMove(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene || !m_active)
        return;

    applyDelta(scene, vp, event);
}

void DuplicateTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    // Same behavior as hover-move (Blender Shift-D style)
    mouseMove(vp, scene, event);
}

void DuplicateTool::mouseUp(Viewport* /*vp*/, Scene* /*scene*/, const CoreEvent& /*event*/)
{
    // no-op (we confirm on LMB mouseDown)
}

bool DuplicateTool::keyPress(Viewport* /*vp*/, Scene* scene, const CoreEvent& event)
{
    if (!scene || !m_active)
        return false;

    // You’ll need to map your key codes; assuming Escape == 27.
    if (event.key_code == 27)
    {
        scene->abortMeshChanges();
        // scene->setActiveTool("SelectTool");

        m_active = false;
        m_sets.clear();
        return true;
    }

    return false;
}

// ============================================================================
// Duplicate logic
// ============================================================================

static std::vector<int32_t> selectedOrAllPolys(const SysMesh* mesh)
{
    if (!mesh)
        return {};

    const auto& sel = mesh->selected_polys();
    if (!sel.empty())
        return std::vector<int32_t>(sel.begin(), sel.end());

    const auto& all = mesh->all_polys();
    return std::vector<int32_t>(all.begin(), all.end());
}

static glm::vec3 computeCenterOfVerts(const SysMesh* mesh, const std::vector<int32_t>& verts)
{
    if (!mesh || verts.empty())
        return glm::vec3(0.0f);

    glm::vec3 sum(0.0f);
    int       cnt = 0;
    for (int32_t v : verts)
    {
        if (!mesh->vert_valid(v))
            continue;

        sum += mesh->vert_position(v);
        ++cnt;
    }
    return (cnt > 0) ? (sum / (float)cnt) : glm::vec3(0.0f);
}

void DuplicateTool::beginDuplicate(Scene* scene)
{
    m_sets.clear();
    m_delta = glm::vec3(0.0f);

    if (!scene)
        return;

    // Capture start mouse on first activation; if your activation doesn’t have an event,
    // you can set this from the first mouseMove call instead. Here we default to 0.
    // You likely call activate() from Shift-D with current cursor coords; if you can,
    // pass them in and store them.
    // For now: we’ll treat the first mouseMove as the initializer if m_mouseStartPx is (0,0) and we haven’t built plane.
    // But we still store it here.
    // NOTE: If you can, set this right when Shift-D happens.
    // m_mouseStartPx = current mouse pos;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm)
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        std::vector<int32_t> basePolys = selectedOrAllPolys(mesh);
        if (basePolys.empty())
            continue;

        // Map IDs (your convention)
        constexpr int kNormMapId = 0;
        constexpr int kUvMapId   = 1;

        const int normMap = mesh->map_find(kNormMapId);
        const int uvMap   = mesh->map_find(kUvMapId);

        const bool hasNormMap = (normMap >= 0 && mesh->map_dim(normMap) == 3);
        const bool hasUvMap   = (uvMap >= 0 && mesh->map_dim(uvMap) == 2);

        // Old->new vert mapping (only for verts referenced by duplicated polys)
        std::unordered_map<int32_t, int32_t> vOldToNew;
        vOldToNew.reserve(basePolys.size() * 4);

        // Store created verts for moving
        MoveSet set{};
        set.mesh = sm;

        // Also track newly created polys so we can reselect them
        std::vector<int32_t> newPolys;
        newPolys.reserve(basePolys.size());

        // Clear selection first (optional; Blender selects new copy)
        // We will select only the duplicated polys.
        mesh->clear_selected_polys();

        for (int32_t pid : basePolys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(pid);
            if (pv.size() < 3)
                continue;

            // Duplicate vertices on demand
            SysPolyVerts newPv{};
            newPv.reserve(pv.size());

            for (int32_t ov : pv)
            {
                auto it = vOldToNew.find(ov);
                if (it == vOldToNew.end())
                {
                    if (!mesh->vert_valid(ov))
                        continue;

                    const glm::vec3 pos = mesh->vert_position(ov);
                    const int32_t   nv  = mesh->create_vert(pos);

                    vOldToNew.emplace(ov, nv);
                    set.movedVerts.push_back(nv);
                    newPv.push_back(nv);
                }
                else
                {
                    newPv.push_back(it->second);
                }
            }

            if (newPv.size() < 3)
                continue;

            // Preserve per-poly material id
            const uint32_t matId = mesh->poly_material(pid);

            const int32_t newPid = mesh->create_poly(newPv, matId);
            newPolys.push_back(newPid);

            // --- Per-corner UV copy (face-varying, NOT shared)
            if (hasUvMap && mesh->map_poly_valid(uvMap, pid))
            {
                const SysPolyVerts& uvPv = mesh->map_poly_verts(uvMap, pid);
                if (uvPv.size() == pv.size())
                {
                    SysPolyVerts newUvPv{};
                    newUvPv.reserve(uvPv.size());

                    for (int i = 0; i < (int)uvPv.size(); ++i)
                    {
                        const int32_t mv = uvPv[i];
                        const float*  p  = mesh->map_vert_position(uvMap, mv);
                        if (!p)
                        {
                            // fallback
                            const float tmp[2] = {0.0f, 0.0f};
                            newUvPv.push_back(mesh->map_create_vert(uvMap, tmp));
                            continue;
                        }

                        const float tmp[2] = {p[0], p[1]};
                        newUvPv.push_back(mesh->map_create_vert(uvMap, tmp));
                    }

                    mesh->map_create_poly(uvMap, newPid, newUvPv);
                }
            }

            // --- Per-corner Normal copy (face-varying, NOT shared)
            if (hasNormMap && mesh->map_poly_valid(normMap, pid))
            {
                const SysPolyVerts& nPv = mesh->map_poly_verts(normMap, pid);
                if (nPv.size() == pv.size())
                {
                    SysPolyVerts newNPv{};
                    newNPv.reserve(nPv.size());

                    for (int i = 0; i < (int)nPv.size(); ++i)
                    {
                        const int32_t mv = nPv[i];
                        const float*  p  = mesh->map_vert_position(normMap, mv);

                        glm::vec3 nn(0.0f, 1.0f, 0.0f);
                        if (p)
                            nn = safe_normalize(glm::vec3(p[0], p[1], p[2]));

                        const float tmp[3] = {nn.x, nn.y, nn.z};
                        newNPv.push_back(mesh->map_create_vert(normMap, tmp));
                    }

                    mesh->map_create_poly(normMap, newPid, newNPv);
                }
            }

            // Select duplicated polys (like Blender)
            mesh->select_poly(newPid, true);
        }

        if (set.movedVerts.empty())
            continue;

        // Cache start positions for stable dragging
        set.startPos.reserve(set.movedVerts.size());
        for (int32_t v : set.movedVerts)
            set.startPos.push_back(mesh->vert_position(v));

        m_sets.push_back(std::move(set));
    }
}

void DuplicateTool::applyDelta(Scene* scene, Viewport* vp, const CoreEvent& event)
{
    (void)scene;

    // Initialize start mouse on first move if not set by activate.
    // If you *can* pass the cursor position into activate(), do it and remove this.
    if (m_mouseStartPx == glm::vec2(0.0f, 0.0f))
        m_mouseStartPx = glm::vec2(event.x, event.y);

    // Find a reasonable drag center (first mesh set)
    SceneMesh* firstSm = (m_sets.empty()) ? nullptr : m_sets[0].mesh;
    SysMesh*   first   = firstSm ? firstSm->sysMesh() : nullptr;

    glm::vec3 center(0.0f);
    if (first && !m_sets.empty())
        center = computeCenterOfVerts(first, m_sets[0].movedVerts);

    // Plane normal: view direction (from ray at start pixel)
    const un::ray   r0     = vp->ray(m_mouseStartPx.x, m_mouseStartPx.y);
    const glm::vec3 planeN = safe_normalize(r0.dir);

    glm::vec3     hit0(0.0f), hit1(0.0f);
    const un::ray r1 = vp->ray(event.x, event.y);

    if (!rayPlaneIntersect(r0, center, planeN, hit0))
        return;
    if (!rayPlaneIntersect(r1, center, planeN, hit1))
        return;

    m_delta = hit1 - hit0;

    // Apply to all duplicated verts across all meshes
    for (MoveSet& set : m_sets)
    {
        if (!set.mesh)
            continue;

        SysMesh* mesh = set.mesh->sysMesh();
        if (!mesh)
            continue;

        const size_t n = std::min(set.movedVerts.size(), set.startPos.size());
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t v = set.movedVerts[i];
            if (!mesh->vert_valid(v))
                continue;

            mesh->move_vert(v, set.startPos[i] + m_delta);
        }
    }
}
