//=============================================================================
// StretchGizmo.cpp
//=============================================================================
#include "StretchGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

StretchGizmo::StretchGizmo(glm::vec3* scale) : m_scale(scale)
{
    if (m_scale)
        *m_scale = glm::vec3{1.0f};
}

glm::vec3 StretchGizmo::axisDir(Mode m) noexcept
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

glm::vec3 StretchGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                             const glm::vec3& origin,
                                             Mode             axisMode,
                                             float            mx,
                                             float            my) const
{
    const glm::vec3 aDir = axisDir(axisMode);
    if (glm::length2(aDir) < 1e-12f)
        return origin;

    // Plane containing the axis and facing the camera.
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = origin - camPos;

    if (glm::length2(viewDir) < 1e-8f)
        viewDir = glm::vec3{0.0f, 0.0f, -1.0f};
    else
        viewDir = glm::normalize(viewDir);

    glm::vec3 n = glm::cross(aDir, viewDir);

    // Degenerate fallback when axis aligns with view direction.
    if (glm::length2(n) < 1e-8f)
    {
        n = glm::cross(aDir, glm::vec3{0.0f, 0.0f, 1.0f});
        if (glm::length2(n) < 1e-8f)
            n = glm::cross(aDir, glm::vec3{0.0f, 1.0f, 0.0f});
    }

    n = glm::normalize(glm::cross(aDir, n)); // plane normal

    glm::vec3 hit = glm::vec3{0.0f};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

void StretchGizmo::buildBillboardSquare(Viewport*        vp,
                                        const glm::vec3& center,
                                        float            halfExtentWorld,
                                        const glm::vec4& color,
                                        bool             filledForPick)
{
    const glm::vec3 r = vp->rightDirection();
    const glm::vec3 u = vp->upDirection();

    const glm::vec3 p0 = center + (-r - u) * halfExtentWorld;
    const glm::vec3 p1 = center + (r - u) * halfExtentWorld;
    const glm::vec3 p2 = center + (r + u) * halfExtentWorld;
    const glm::vec3 p3 = center + (-r + u) * halfExtentWorld;

    if (filledForPick)
    {
        std::vector<glm::vec3> poly = {p0, p1, p2, p3};
        m_overlayHandler.add_polygon(poly, color);
        return;
    }

    m_overlayHandler.add_line(p0, p1, 4.0f, color);
    m_overlayHandler.add_line(p1, p2, 4.0f, color);
    m_overlayHandler.add_line(p2, p3, 4.0f, color);
    m_overlayHandler.add_line(p3, p0, 4.0f, color);
}

void StretchGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_scale)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_mode               = modeFromHandle(handle);
    m_dragging           = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    m_startScale = *m_scale;

    m_baseOrigin = sel::selection_center_bounds(scene);
    m_origin     = m_baseOrigin;

    if (m_mode == Mode::Uniform)
    {
        m_startMx = ev.x;
        m_startMy = ev.y;

        m_startParam = std::max(1e-6f, m_startScale.x);
        m_axisDir    = glm::vec3{0.0f};
        return;
    }

    m_axisDir    = axisDir(m_mode);
    m_startHit   = dragPointOnAxisPlane(vp, m_origin, m_mode, ev.x, ev.y);
    m_startParam = glm::dot(m_startHit - m_origin, m_axisDir);

    if (std::abs(m_startParam) < 1e-6f)
        m_startParam = (m_startParam < 0.0f) ? -1e-6f : 1e-6f;
}

void StretchGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_scale)
        return;

    if (!m_dragging || m_mode == Mode::None)
        return;

    glm::vec3 s = m_startScale;

    if (m_mode == Mode::Uniform)
    {
        const float dy = (m_startMy - ev.y);

        const float pixelsPerDoubling = 120.0f;
        const float k                 = 1.0f / pixelsPerDoubling;

        const float factor = std::pow(2.0f, dy * k);
        const float s0     = std::max(0.0001f, m_startScale.x);

        const float sNew = glm::clamp(s0 * factor, 0.0001f, 10000.0f);

        *m_scale = glm::vec3{sNew, sNew, sNew};
        return;
    }

    const glm::vec3 curHit   = dragPointOnAxisPlane(vp, m_origin, m_mode, ev.x, ev.y);
    const float     curParam = glm::dot(curHit - m_origin, m_axisDir);

    const float k        = curParam / m_startParam;
    const float kClamped = glm::clamp(k, 0.0001f, 10000.0f);

    if (m_mode == Mode::X)
        s.x *= kClamped;
    if (m_mode == Mode::Y)
        s.y *= kClamped;
    if (m_mode == Mode::Z)
        s.z *= kClamped;

    *m_scale = s;
}

void StretchGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void StretchGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_scale)
        return;

    if (!m_dragging)
        m_origin = sel::selection_center_bounds(scene);

    const glm::vec3 origin = m_origin;

    const float px = vp->pixelScale(origin);

    m_centerHalfWorld  = std::max(0.0001f, px * 10.0f);
    m_axisLenWorld     = std::max(0.05f, px * 70.0f);
    m_axisBoxHalfWorld = std::max(0.0001f, px * 7.0f);

    m_overlayHandler.clear();

    // Center handle (uniform) - 3
    {
        m_overlayHandler.begin_overlay(static_cast<int32_t>(Mode::Uniform));

        const float pickHalf = m_centerHalfWorld * 1.35f;

        buildBillboardSquare(vp, origin, pickHalf, glm::vec4{1, 1, 1, 0.2f}, true);
        buildBillboardSquare(vp, origin, m_centerHalfWorld, glm::vec4{1, 1, 1, 1}, false);

        m_overlayHandler.set_axis(glm::vec3{0.0f});
        m_overlayHandler.end_overlay();
    }

    auto addAxis = [&](Mode mode, const glm::vec3& dir, const glm::vec4& color) {
        const int32_t h = static_cast<int32_t>(mode);

        const glm::vec3 stemA = origin + dir * m_centerHalfWorld;
        const glm::vec3 stemB = origin + dir * (m_centerHalfWorld + m_axisLenWorld);

        m_overlayHandler.begin_overlay(h);

        m_overlayHandler.add_line(stemA, stemB, 8.0f, color);

        const glm::vec3 tipCenter = stemB;
        buildBillboardSquare(vp, tipCenter, m_axisBoxHalfWorld, glm::vec4{color.r, color.g, color.b, 0.25f}, true);
        buildBillboardSquare(vp, tipCenter, m_axisBoxHalfWorld, color, false);

        m_overlayHandler.set_axis(dir);
        m_overlayHandler.end_overlay();
    };

    addAxis(Mode::X, glm::vec3{1, 0, 0}, glm::vec4{1, 0, 0, 1});
    addAxis(Mode::Y, glm::vec3{0, 1, 0}, glm::vec4{0, 1, 0, 1});
    addAxis(Mode::Z, glm::vec3{0, 0, 1}, glm::vec4{0, 0, 1, 1});
}
