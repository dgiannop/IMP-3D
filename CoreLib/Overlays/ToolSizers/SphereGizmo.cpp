//=============================================================================
// SphereGizmo.cpp
//=============================================================================
#include "SphereGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

SphereGizmo::SphereGizmo(glm::vec3* center, glm::vec3* radius) : m_center(center), m_radius(radius)
{
}

glm::vec3 SphereGizmo::safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
{
    if (glm::length2(v) < 1e-12f)
        return fallback;
    return glm::normalize(v);
}

float SphereGizmo::clampMin(float v, float minV) noexcept
{
    if (v < minV)
        return minV;
    return v;
}

glm::vec3 SphereGizmo::axisDir(Mode m) noexcept
{
    switch (m)
    {
        case Mode::X:
            return glm::vec3{1.f, 0.f, 0.f};
        case Mode::Y:
            return glm::vec3{0.f, 1.f, 0.f};
        case Mode::Z:
            return glm::vec3{0.f, 0.f, 1.f};
        default:
            return glm::vec3{0.f};
    }
}

int SphereGizmo::axisIndex(Mode m) noexcept
{
    switch (m)
    {
        case Mode::X:
            return 0;
        case Mode::Y:
            return 1;
        case Mode::Z:
            return 2;
        default:
            return -1;
    }
}

glm::vec3 SphereGizmo::dragPointOnViewPlane(Viewport* vp, const glm::vec3& origin, float mx, float my) const
{
    glm::vec3 hit = glm::vec3{0.f};
    if (vp->rayViewPlaneHit(mx, my, origin, hit))
        return hit;
    return origin;
}

glm::vec3 SphereGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                            const glm::vec3& origin,
                                            const glm::vec3& axis,
                                            float            mx,
                                            float            my) const
{
    // Stable plane containing axis, aligned to view as much as possible:
    // n = cross(axis, cross(viewDir, axis))
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = camPos - origin; // origin -> camera
    viewDir                 = safeNormalize(viewDir, glm::vec3{0.f, 0.f, 1.f});

    glm::vec3 n = glm::cross(axis, glm::cross(viewDir, axis));

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

void SphereGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_radius)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);

    m_mode     = modeFromHandle(handle);
    m_dragging = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    m_origin      = *m_center;
    m_startCenter = *m_center;
    m_startRadius = *m_radius;

    // Normalise stored radii to avoid negative/zero weirdness.
    m_startRadius.x = clampMin(m_startRadius.x, m_minRadius);
    m_startRadius.y = clampMin(m_startRadius.y, m_minRadius);
    m_startRadius.z = clampMin(m_startRadius.z, m_minRadius);

    m_startUniformRadius = std::max(m_startRadius.x, std::max(m_startRadius.y, m_startRadius.z));

    if (m_mode == Mode::Center)
    {
        m_startOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        return;
    }

    m_axisDir    = axisDir(m_mode);
    m_startHit   = dragPointOnAxisPlane(vp, m_origin, m_axisDir, ev.x, ev.y);
    m_startParam = glm::dot(m_startHit - m_origin, m_axisDir);
}

void SphereGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_radius)
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

    glm::vec3 r = m_startRadius;

    if (!ev.alt_key)
    {
        // Uniform scaling by default.
        const float u = clampMin(m_startUniformRadius + delta, m_minRadius);
        r             = glm::vec3{u, u, u};
    }
    else
    {
        // Per-axis scaling when Alt is held.
        const int ai = axisIndex(m_mode);
        if (ai >= 0)
            r[ai] = clampMin(r[ai] + delta, m_minRadius);
    }

    *m_radius = r;
}

void SphereGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void SphereGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_center || !m_radius)
        return;

    const glm::vec3 origin = *m_center;

    glm::vec3 rRaw = *m_radius;
    rRaw.x         = clampMin(rRaw.x, m_minRadius);
    rRaw.y         = clampMin(rRaw.y, m_minRadius);
    rRaw.z         = clampMin(rRaw.z, m_minRadius);

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

    auto addAxis = [&](Mode mode, const glm::vec3& dir, float extentWorld, const glm::vec4& color) {
        const int32_t h = static_cast<int32_t>(mode);

        const float axisLen = std::max(minVisualLen, extentWorld);

        const glm::vec3 stemA  = origin + dir * m_centerRadiusWorld;
        const glm::vec3 tipPos = origin + dir * axisLen;

        m_overlayHandler.begin_overlay(h);

        m_overlayHandler.add_line(stemA, tipPos, 4.0f, color);

        m_overlayHandler.set_axis(faceN);
        m_overlayHandler.add_filled_circle(tipPos,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 0.25f},
                                           2.0f,
                                           48);

        m_overlayHandler.add_filled_circle(tipPos,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 1.f},
                                           2.0f,
                                           48);

        m_overlayHandler.set_axis(dir);
        m_overlayHandler.end_overlay();
    };

    addAxis(Mode::X, glm::vec3{1, 0, 0}, rRaw.x, glm::vec4{1, 0, 0, 1});
    addAxis(Mode::Y, glm::vec3{0, 1, 0}, rRaw.y, glm::vec4{0, 1, 0, 1});
    addAxis(Mode::Z, glm::vec3{0, 0, 1}, rRaw.z, glm::vec4{0, 0, 1, 1});
}
