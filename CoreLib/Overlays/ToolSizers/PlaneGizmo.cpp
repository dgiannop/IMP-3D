//=============================================================================
// PlaneGizmo.cpp
//=============================================================================
#include "PlaneGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "CoreUtilities.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

PlaneGizmo::PlaneGizmo(glm::vec3* center, glm::vec2* size, glm::ivec3* axis) :
    m_center{center},
    m_size{size},
    m_axis{axis}
{
}

float PlaneGizmo::clampMin(float v, float minV) noexcept
{
    if (v < minV)
        return minV;
    return v;
}

void PlaneGizmo::computePlaneFrame(glm::vec3& outN, glm::vec3& outU, glm::vec3& outV) const noexcept
{
    glm::vec3 N = glm::vec3(0.f, 1.f, 0.f);

    if (m_axis)
    {
        glm::vec3 a = glm::vec3(*m_axis);
        if (!un::is_zero(a))
            N = a;
    }

    N = un::safe_normalize(N);

    glm::vec3 helper = (std::abs(N.y) < 0.9f) ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);

    glm::vec3 U = glm::cross(helper, N);
    if (un::is_zero(U))
        U = glm::vec3(1.f, 0.f, 0.f);
    U = un::safe_normalize(U);

    glm::vec3 V = glm::cross(N, U);
    if (un::is_zero(V))
        V = glm::vec3(0.f, 0.f, 1.f);
    V = un::safe_normalize(V);

    outN = N;
    outU = U;
    outV = V;
}

glm::vec3 PlaneGizmo::dragPointOnViewPlane(Viewport* vp, const glm::vec3& origin, float mx, float my) const
{
    glm::vec3 hit = glm::vec3{0.f};
    if (vp->rayViewPlaneHit(mx, my, origin, hit))
        return hit;
    return origin;
}

glm::vec3 PlaneGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                           const glm::vec3& origin,
                                           const glm::vec3& axisDir,
                                           float            mx,
                                           float            my) const
{
    // Stable plane containing axisDir and aligned to camera view as much as possible:
    // n = cross(axis, cross(viewDir, axis))
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = camPos - origin; // origin -> camera
    viewDir                 = un::safe_normalize(viewDir, glm::vec3{0.f, 0.f, 1.f});

    glm::vec3 n = glm::cross(axisDir, glm::cross(viewDir, axisDir));

    if (glm::length2(n) < 1e-10f)
    {
        n = glm::cross(axisDir, glm::vec3{0.f, 0.f, 1.f});
        if (glm::length2(n) < 1e-10f)
            n = glm::cross(axisDir, glm::vec3{0.f, 1.f, 0.f});
    }

    n = un::safe_normalize(n, glm::vec3{0.f, 0.f, 1.f});

    glm::vec3 hit = glm::vec3{0.f};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

void PlaneGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_size)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);

    m_mode     = modeFromHandle(handle);
    m_dragging = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    m_origin      = *m_center;
    m_startCenter = *m_center;

    m_startSize   = *m_size;
    m_startSize.x = clampMin(m_startSize.x, m_minSize);
    m_startSize.y = clampMin(m_startSize.y, m_minSize);

    if (m_mode == Mode::Center)
    {
        m_startOnPlane = dragPointOnViewPlane(vp, m_origin, ev.x, ev.y);
        return;
    }

    glm::vec3 N, U, V;
    computePlaneFrame(N, U, V);

    m_axisDir = (m_mode == Mode::U) ? U : V;

    m_startHit   = dragPointOnAxisPlane(vp, m_origin, m_axisDir, ev.x, ev.y);
    m_startParam = glm::dot(m_startHit - m_origin, m_axisDir);
}

void PlaneGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_center || !m_size)
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

    glm::vec2 sz = m_startSize;

    // Handle is at +half-size, but tool parameter stores full size => size += 2*delta
    if (m_mode == Mode::U)
        sz.x = clampMin(sz.x + (2.0f * delta), m_minSize);
    else if (m_mode == Mode::V)
        sz.y = clampMin(sz.y + (2.0f * delta), m_minSize);

    *m_size = sz;
}

void PlaneGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void PlaneGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_center || !m_size)
        return;

    const glm::vec3 origin = *m_center;

    glm::vec2 sz = *m_size;
    sz.x         = clampMin(sz.x, m_minSize);
    sz.y         = clampMin(sz.y, m_minSize);

    glm::vec3 N, U, V;
    computePlaneFrame(N, U, V);

    const float px = vp->pixelScale(origin);

    m_centerRadiusWorld = std::max(0.0001f, px * 14.0f);
    m_tipRadiusWorld    = std::max(0.0001f, px * 7.0f);

    const float minVisualLen = m_centerRadiusWorld + (m_tipRadiusWorld * 1.75f);

    m_overlayHandler.clear();

    // Camera-facing normal for disks
    const glm::vec3 right = vp->rightDirection();
    const glm::vec3 up    = vp->upDirection();
    const glm::vec3 faceN = un::safe_normalize(glm::cross(right, up), glm::vec3{0.f, 0.f, 1.f});

    // Center disk (handle 2)
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

    auto addAxis = [&](Mode mode, const glm::vec3& dir, float halfExtent, const glm::vec4& color) {
        const int32_t h = static_cast<int32_t>(mode);

        const float axisLen = std::max(minVisualLen, halfExtent);

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

    // Show tips at +half-size along plane axes
    addAxis(Mode::U, U, 0.5f * sz.x, glm::vec4{1.f, 0.f, 0.f, 1.f}); // red
    addAxis(Mode::V, V, 0.5f * sz.y, glm::vec4{0.f, 0.f, 1.f, 1.f}); // blue
}
