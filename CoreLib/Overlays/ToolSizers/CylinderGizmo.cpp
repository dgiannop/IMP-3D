//=============================================================================
// CylinderGizmo.cpp
//=============================================================================
#include "CylinderGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

CylinderGizmo::CylinderGizmo(glm::vec3* center, float* radius, float* height) :
    m_center{center},
    m_radius{radius},
    m_height{height}
{
}

glm::vec3 CylinderGizmo::safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
{
    if (glm::length2(v) < 1e-12f)
        return fallback;
    return glm::normalize(v);
}

float CylinderGizmo::clampMin(float v, float minV) noexcept
{
    if (v < minV)
        return minV;
    return v;
}

glm::vec3 CylinderGizmo::axisDir(Mode m) noexcept
{
    switch (m)
    {
        case Mode::RadX:
            return glm::vec3{1.f, 0.f, 0.f};
        case Mode::HalfY:
            return glm::vec3{0.f, 1.f, 0.f};
        case Mode::RadZ:
            return glm::vec3{0.f, 0.f, 1.f};
        default:
            return glm::vec3{0.f};
    }
}

glm::vec3 CylinderGizmo::dragPointOnViewPlane(Viewport* vp, const glm::vec3& origin, float mx, float my) const
{
    glm::vec3 hit = glm::vec3{0.f};
    if (vp->rayViewPlaneHit(mx, my, origin, hit))
        return hit;
    return origin;
}

glm::vec3 CylinderGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                              const glm::vec3& origin,
                                              const glm::vec3& axis,
                                              float            mx,
                                              float            my) const
{
    // We want a plane that contains the axis line and is stable w.r.t. the view.
    // Use: n = cross(axis, cross(viewDir, axis))
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = camPos - origin; // from origin -> camera
    viewDir                 = safeNormalize(viewDir, glm::vec3{0.f, 0.f, 1.f});

    glm::vec3 n = glm::cross(axis, glm::cross(viewDir, axis));

    // Fallback if axis aligns with viewDir.
    if (glm::length2(n) < 1e-10f)
    {
        n = glm::cross(axis, glm::vec3{0.f, 0.f, 1.f});
        if (glm::length2(n) < 1e-10f)
            n = glm::cross(axis, glm::vec3{0.f, 1.f, 0.f});
    }

    n = safeNormalize(n, glm::vec3{0.f, 0.f, 1.f});

    glm::vec3 hit = glm::vec3{0.f};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

void CylinderGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_radius || !m_height)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);

    m_mode     = modeFromHandle(handle);
    m_dragging = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    m_origin      = *m_center;
    m_startCenter = *m_center;

    m_startRadius = clampMin(*m_radius, m_minRadius);
    m_startHeight = clampMin(*m_height, m_minHeight);

    if (m_mode == Mode::Center)
    {
        m_startOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        return;
    }

    m_axisDir    = axisDir(m_mode);
    m_startHit   = dragPointOnAxisPlane(vp, m_origin, m_axisDir, ev.x, ev.y);
    m_startParam = glm::dot(m_startHit - m_origin, m_axisDir);
}

void CylinderGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_radius || !m_height)
        return;

    if (!m_dragging || m_mode == Mode::None)
        return;

    if (m_mode == Mode::Center)
    {
        const glm::vec3 curOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        const glm::vec3 d          = curOnPlane - m_startOnPlane;

        *m_center = m_startCenter + d;
        return;
    }

    const glm::vec3 curHit   = dragPointOnAxisPlane(vp, m_origin, m_axisDir, ev.x, ev.y);
    const float     curParam = glm::dot(curHit - m_origin, m_axisDir);
    const float     delta    = (curParam - m_startParam);

    if (m_mode == Mode::RadX || m_mode == Mode::RadZ)
    {
        // Radius changes directly along axis delta.
        const float r = clampMin(m_startRadius + delta, m_minRadius);
        *m_radius     = r;
        return;
    }

    if (m_mode == Mode::HalfY)
    {
        // Handle is at +halfHeight, but tool parameter is full height.
        // delta is in "half" space => height += 2*delta
        const float h = clampMin(m_startHeight + (2.0f * delta), m_minHeight);
        *m_height     = h;
        return;
    }
}

void CylinderGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void CylinderGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_center || !m_radius || !m_height)
        return;

    const glm::vec3 origin = *m_center;

    const float r  = clampMin(*m_radius, m_minRadius);
    const float h  = clampMin(*m_height, m_minHeight);
    const float hy = 0.5f * h;

    const float px = vp->pixelScale(origin);

    m_centerRadiusWorld = std::max(0.0001f, px * 14.0f);
    m_tipRadiusWorld    = std::max(0.0001f, px * 7.0f);

    const float minVisualLen = m_centerRadiusWorld + (m_tipRadiusWorld * 1.75f);

    m_overlayHandler.clear();

    const glm::vec3 right = vp->rightDirection();
    const glm::vec3 up    = vp->upDirection();
    const glm::vec3 faceN = safeNormalize(glm::cross(right, up), glm::vec3{0.f, 0.f, 1.f});

    // Center disk (handle 3)
    {
        m_overlayHandler.begin_overlay(static_cast<int32_t>(Mode::Center));

        m_overlayHandler.set_axis(faceN);
        m_overlayHandler.add_filled_circle(origin,
                                           m_centerRadiusWorld,
                                           glm::vec4{1.f, 1.f, 1.f, 0.85f},
                                           2.0f,
                                           48);

        m_overlayHandler.add_filled_circle(origin,
                                           m_centerRadiusWorld,
                                           glm::vec4{1.f, 1.f, 1.f, 1.f},
                                           2.0f,
                                           48);

        m_overlayHandler.set_axis(glm::vec3{0.f});
        m_overlayHandler.end_overlay();
    }

    auto addAxisTip = [&](Mode mode, const glm::vec3& dir, float extentWorld, const glm::vec4& color) {
        const int32_t hnd = static_cast<int32_t>(mode);

        const float     axisLen = std::max(minVisualLen, extentWorld);
        const glm::vec3 stemA   = origin + dir * m_centerRadiusWorld;
        const glm::vec3 tipPos  = origin + dir * axisLen;

        m_overlayHandler.begin_overlay(hnd);

        m_overlayHandler.add_line(stemA, tipPos, 4.0f, color);

        m_overlayHandler.set_axis(faceN);
        m_overlayHandler.add_filled_circle(tipPos,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 0.25f},
                                           2.0f,
                                           48);

        m_overlayHandler.add_filled_circle(tipPos,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 1.0f},
                                           2.0f,
                                           48);

        m_overlayHandler.set_axis(dir);
        m_overlayHandler.end_overlay();
    };

    // Radius tips in X/Z, half-height tip in Y (at +hy)
    addAxisTip(Mode::RadX, glm::vec3{1, 0, 0}, r, glm::vec4{1, 0, 0, 1});
    addAxisTip(Mode::HalfY, glm::vec3{0, 1, 0}, hy, glm::vec4{0, 1, 0, 1});
    addAxisTip(Mode::RadZ, glm::vec3{0, 0, 1}, r, glm::vec4{0, 0, 1, 1});
}
