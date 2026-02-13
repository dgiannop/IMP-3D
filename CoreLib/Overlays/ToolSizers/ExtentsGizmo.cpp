//=============================================================================
// ExtentsGizmo.cpp
//=============================================================================
#include "ExtentsGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

ExtentsGizmo::ExtentsGizmo(glm::vec3* center, glm::vec3* extents) : m_center(center), m_extents(extents)
{
    // The tool owns initial values; the gizmo only edits them.
}

float ExtentsGizmo::clampExtent(float v, float minV) noexcept
{
    // Extents/radii are non-negative by convention.
    if (v < minV)
        return minV;
    return v;
}

glm::vec3 ExtentsGizmo::axisDir(Mode m) noexcept
{
    switch (m)
    {
        case Mode::X:
            return glm::vec3{1.0f, 0.0f, 0.0f};
        case Mode::Y:
            return glm::vec3{0.0f, 1.0f, 0.0f};
        case Mode::Z:
            return glm::vec3{0.0f, 0.0f, 1.0f};
        default:
            return glm::vec3{0.0f};
    }
}

glm::vec3 ExtentsGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                             const glm::vec3& origin,
                                             Mode             axisMode,
                                             float            mx,
                                             float            my) const
{
    const glm::vec3 aDir = axisDir(axisMode);
    if (glm::length2(aDir) < 1e-12f)
        return origin;

    // Plane containing the axis and facing the camera as much as possible.
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = origin - camPos;
    viewDir                 = un::safe_normalize(viewDir, glm::vec3{0.0f, 0.0f, -1.0f});

    glm::vec3 n = glm::cross(aDir, viewDir);

    // Degenerate fallback when axis aligns with view direction.
    if (glm::length2(n) < 1e-10f)
    {
        n = glm::cross(aDir, glm::vec3{0.0f, 0.0f, 1.0f});
        if (glm::length2(n) < 1e-10f)
            n = glm::cross(aDir, glm::vec3{0.0f, 1.0f, 0.0f});
    }

    n = un::safe_normalize(glm::cross(aDir, n), glm::vec3{0.0f, 0.0f, 1.0f});

    glm::vec3 hit = glm::vec3{0.0f};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

glm::vec3 ExtentsGizmo::dragPointOnViewPlane(Viewport*        vp,
                                             const glm::vec3& origin,
                                             float            mx,
                                             float            my) const
{
    glm::vec3 hit = glm::vec3{0.0f};
    if (vp->rayViewPlaneHit(mx, my, origin, hit))
        return hit;

    return origin;
}

void ExtentsGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_extents)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);

    m_mode     = modeFromHandle(handle);
    m_dragging = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    m_origin       = *m_center;
    m_startCenter  = *m_center;
    m_startExtents = *m_extents;

    if (m_mode == Mode::Center)
    {
        m_startOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        return;
    }

    m_axisDir    = axisDir(m_mode);
    m_startHit   = dragPointOnAxisPlane(vp, m_origin, m_mode, ev.x, ev.y);
    m_startParam = glm::dot(m_startHit - m_origin, m_axisDir);
}

void ExtentsGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_extents)
        return;

    if (!m_dragging || m_mode == Mode::None)
        return;

    // Center handle: free move in view plane.
    if (m_mode == Mode::Center)
    {
        const glm::vec3 curOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        const glm::vec3 d          = curOnPlane - m_startOnPlane;

        *m_center = m_startCenter + d;
        return;
    }

    // Axis handle: edit the corresponding extent component. Center stays fixed.
    const glm::vec3 curHit   = dragPointOnAxisPlane(vp, m_origin, m_mode, ev.x, ev.y);
    const float     curParam = glm::dot(curHit - m_origin, m_axisDir);

    const float delta = (curParam - m_startParam);

    glm::vec3 e = m_startExtents;

    if (m_mode == Mode::X)
        e.x = clampExtent(e.x + delta, m_minExtent);
    else if (m_mode == Mode::Y)
        e.y = clampExtent(e.y + delta, m_minExtent);
    else if (m_mode == Mode::Z)
        e.z = clampExtent(e.z + delta, m_minExtent);

    *m_extents = e;
}

void ExtentsGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void ExtentsGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_center || !m_extents)
        return;

    const glm::vec3 origin = *m_center;

    // Extents/radii are expected non-negative.
    const glm::vec3 eRaw = *m_extents;
    const glm::vec3 e    = glm::vec3{
        clampExtent(eRaw.x, m_minExtent),
        clampExtent(eRaw.y, m_minExtent),
        clampExtent(eRaw.z, m_minExtent),
    };

    const float px = vp->pixelScale(origin);

    // Tuned to match the other gizmos (Translate/Stretch/NormalPull).
    m_centerRadiusWorld = std::max(0.0001f, px * 14.0f); // ~14px
    m_tipRadiusWorld    = std::max(0.0001f, px * 7.0f);  // ~14px diameter

    // Minimum visible axis length so tips don't collapse into the center disk.
    const float minVisualLen = m_centerRadiusWorld + (m_tipRadiusWorld * 1.75f);

    m_overlayHandler.clear();

    // Camera-facing normal for billboarded disks.
    const glm::vec3 right = vp->rightDirection();
    const glm::vec3 up    = vp->upDirection();
    const glm::vec3 faceN = un::safe_normalize(glm::cross(right, up), glm::vec3{0.0f, 0.0f, 1.0f});

    // -----------------------------------------------------------------
    // Center disk (free move) - handle 3
    // -----------------------------------------------------------------
    {
        m_overlayHandler.begin_overlay(static_cast<int32_t>(Mode::Center));

        // Filled disk so the interior is hittable.
        m_overlayHandler.set_axis(faceN);
        m_overlayHandler.add_filled_circle(origin,
                                           m_centerRadiusWorld,
                                           glm::vec4{1.0f, 1.0f, 1.0f, 0.85f},
                                           2.0f,
                                           48);

        // Crisp outline.
        m_overlayHandler.add_filled_circle(origin,
                                           m_centerRadiusWorld,
                                           glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                                           2.0f,
                                           48);

        m_overlayHandler.set_axis(glm::vec3{0.0f});
        m_overlayHandler.end_overlay();
    }

    // -----------------------------------------------------------------
    // Axis extents (tips at origin + axis * extent)
    // Handles: 0=X, 1=Y, 2=Z
    // -----------------------------------------------------------------
    auto addAxis = [&](Mode mode, const glm::vec3& dir, float extentWorld, const glm::vec4& color) {
        const int32_t h = static_cast<int32_t>(mode);

        // Keep the tip visible even for tiny extents.
        const float axisLen = std::max(minVisualLen, extentWorld);

        const glm::vec3 stemA  = origin + dir * m_centerRadiusWorld;
        const glm::vec3 tipPos = origin + dir * axisLen;

        m_overlayHandler.begin_overlay(h);

        // Thin stem (consistent with other gizmos).
        m_overlayHandler.add_line(stemA, tipPos, 4.0f, color);

        // Tip: camera-facing filled disk (interior used for picking).
        m_overlayHandler.set_axis(faceN);
        m_overlayHandler.add_filled_circle(tipPos,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 0.25f},
                                           2.0f,
                                           48);

        // Crisp outline.
        m_overlayHandler.add_filled_circle(tipPos,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 1.0f},
                                           2.0f,
                                           48);

        // Axis hint remains the actual axis direction for tool logic.
        m_overlayHandler.set_axis(dir);

        m_overlayHandler.end_overlay();
    };

    addAxis(Mode::X, glm::vec3{1, 0, 0}, e.x, glm::vec4{1, 0, 0, 1});
    addAxis(Mode::Y, glm::vec3{0, 1, 0}, e.y, glm::vec4{0, 1, 0, 1});
    addAxis(Mode::Z, glm::vec3{0, 0, 1}, e.z, glm::vec4{0, 0, 1, 1});
}
