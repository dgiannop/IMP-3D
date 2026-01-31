//=============================================================================
// TranslateGizmo.cpp
//=============================================================================
#include "TranslateGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

TranslateGizmo::TranslateGizmo(glm::vec3* amount) : m_amount(amount)
{
    if (m_amount)
        *m_amount = glm::vec3(0.0f);
}

glm::vec3 TranslateGizmo::axisDir(Mode m) noexcept
{
    switch (m)
    {
        case Mode::X:
            return glm::vec3(1.0f, 0.0f, 0.0f);
        case Mode::Y:
            return glm::vec3(0.0f, 1.0f, 0.0f);
        case Mode::Z:
            return glm::vec3(0.0f, 0.0f, 1.0f);
        default:
            return glm::vec3(0.0f);
    }
}

glm::vec3 TranslateGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                               const glm::vec3& origin,
                                               Mode             axisMode,
                                               float            mx,
                                               float            my) const
{
    // Axis direction in world space.
    const glm::vec3 aDir = axisDir(axisMode);
    if (glm::length2(aDir) < 1e-12f)
        return origin;

    // Build a plane that:
    //  - contains the axis direction
    //  - faces the camera as much as possible
    //
    // We construct a plane normal n that is:
    //   - perpendicular to the axis
    //   - as aligned with the camera view as possible
    //
    // This matches the common gizmo constraint plane used by DCC apps.
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = origin - camPos;

    if (glm::length2(viewDir) < 1e-8f)
        viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        viewDir = glm::normalize(viewDir);

    glm::vec3 n = glm::cross(aDir, viewDir);

    // If view is nearly colinear with the axis, pick a fallback to define a stable plane.
    if (glm::length2(n) < 1e-8f)
    {
        n = glm::cross(aDir, glm::vec3(0.0f, 0.0f, 1.0f));
        if (glm::length2(n) < 1e-8f)
            n = glm::cross(aDir, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Plane normal is perpendicular to axis and stable.
    n = glm::normalize(glm::cross(aDir, n));

    glm::vec3 hit(0.0f);
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

glm::vec3 TranslateGizmo::dragPointOnViewPlane(Viewport*        vp,
                                               const glm::vec3& origin,
                                               float            mx,
                                               float            my) const
{
    glm::vec3 hit(0.0f);
    if (vp->rayViewPlaneHit(mx, my, origin, hit))
        return hit;

    return origin;
}

void TranslateGizmo::buildCenterDisk(Viewport*        vp,
                                     const glm::vec3& origin,
                                     float            radiusWorld,
                                     const glm::vec4& color)
{
    // Disk lies in the camera-facing plane (screen plane):
    // use camera right/up as basis vectors.
    const glm::vec3 right = vp->rightDirection();
    const glm::vec3 up    = vp->upDirection();

    // Segment count tuned for a clean circle without being heavy.
    constexpr int kSegs = 48;

    std::vector<glm::vec3> pts;
    pts.resize(kSegs);

    for (int i = 0; i < kSegs; ++i)
    {
        const float t = (float(i) / float(kSegs)) * 6.28318530718f; // 2*pi
        const float c = std::cos(t);
        const float s = std::sin(t);

        pts[i] = origin + right * (c * radiusWorld) + up * (s * radiusWorld);
    }

    // Polygon interior is hittable (so clicking inside the disk selects Free mode).
    m_overlayHandler.add_polygon(pts, color);
}

void TranslateGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);

    m_mode     = modeFromHandle(handle);
    m_dragging = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    // Absolute parameter dragging.
    m_startAmount = *m_amount;

    // Selection center already includes the current deformation; subtract amount to get a stable base origin.
    const glm::vec3 curCenter = sel::selection_center_bounds(scene);
    m_baseOrigin              = curCenter - m_startAmount;

    // Drag origin is the pivot at the current parameter value.
    m_origin = m_baseOrigin + m_startAmount;

    if (m_mode == Mode::Free)
    {
        m_axisDir      = glm::vec3(0.0f);
        m_startOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
    }
    else
    {
        m_axisDir      = axisDir(m_mode);
        m_startOnPlane = dragPointOnAxisPlane(vp, m_origin, m_mode, ev.x, ev.y);
    }
}

void TranslateGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    if (!m_dragging || m_mode == Mode::None)
        return;

    if (m_mode == Mode::Free)
    {
        const glm::vec3 curOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        const glm::vec3 d          = curOnPlane - m_startOnPlane;

        *m_amount = m_startAmount + d;
        return;
    }

    const glm::vec3 curOnPlane = dragPointOnAxisPlane(vp, m_origin, m_mode, ev.x, ev.y);

    const glm::vec3 dPlane = curOnPlane - m_startOnPlane;
    const float     tAxis  = glm::dot(dPlane, m_axisDir);

    *m_amount = m_startAmount + (m_axisDir * tAxis);
}

void TranslateGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void TranslateGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_amount)
        return;

    // Keep baseOrigin tracking when not dragging (so gizmo follows selection changes).
    if (!m_dragging)
    {
        const glm::vec3 curCenter = sel::selection_center_bounds(scene);
        m_baseOrigin              = curCenter - *m_amount;
    }

    const glm::vec3 origin = m_baseOrigin + *m_amount;

    // Convert pixel sizes to world units at the pivot.
    const float px = vp->pixelScale(origin);

    // Tuned to "Blender-ish" proportions.
    m_centerRadiusWorld = std::max(0.0001f, px * 14.0f); // ~14px disk radius
    m_axisLengthWorld   = std::max(0.05f, px * 85.0f);   // ~85px axis length

    m_overlayHandler.clear();

    // -----------------------------------------------------------------
    // Center disk (free move) - handle 3
    // -----------------------------------------------------------------
    {
        m_overlayHandler.begin_overlay((int32_t)Mode::Free);

        // Slightly translucent disk.
        buildCenterDisk(vp, origin, m_centerRadiusWorld, glm::vec4(1.0f, 1.0f, 1.0f, 0.85f));

        // Axis hint is meaningless for free-move.
        m_overlayHandler.set_axis(glm::vec3(0.0f));
        m_overlayHandler.end_overlay();
    }

    // -----------------------------------------------------------------
    // Axis stems: start at disk boundary and extend outward
    // Handles: 0=X, 1=Y, 2=Z
    // -----------------------------------------------------------------
    auto addAxis = [&](Mode mode, const glm::vec3& dir, const glm::vec4& color) {
        const glm::vec3 p0 = origin + dir * m_centerRadiusWorld;
        const glm::vec3 p1 = origin + dir * (m_centerRadiusWorld + m_axisLengthWorld);

        m_overlayHandler.begin_overlay((int32_t)mode);
        m_overlayHandler.add_line(p0, p1, 8.0f, color);
        m_overlayHandler.set_axis(dir);
        m_overlayHandler.end_overlay();
    };

    addAxis(Mode::X, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    addAxis(Mode::Y, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    addAxis(Mode::Z, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
}
