//=============================================================================
// ScaleGizmo.cpp
//=============================================================================
#include "ScaleGizmo.hpp"

#include <algorithm>
#include <cmath>

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

ScaleGizmo::ScaleGizmo(glm::vec3* scale) : m_scale(scale)
{
    if (m_scale)
        *m_scale = glm::vec3{1.0f};
}

void ScaleGizmo::buildBillboardSquare(Viewport*        vp,
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

void ScaleGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_scale)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_mode               = modeFromHandle(handle);
    m_dragging           = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    m_startScale = *m_scale;

    // Pivot.
    m_origin = sel::selection_center_bounds(scene);

    // Screen-space anchor.
    m_startMx = ev.x;
    m_startMy = ev.y;
}

glm::vec3 ScaleGizmo::axisDir(Mode m) noexcept
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

void ScaleGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_scale)
        return;

    if (!m_dragging || m_mode == Mode::None)
        return;

    // Tune: pixels for doubling.
    const float pixelsPerDoubling = 120.0f;
    const float k                 = 1.0f / pixelsPerDoubling;

    // Mouse delta in "screen-up" coordinates.
    const float     dx = (ev.x - m_startMx);
    const float     dy = (ev.y - m_startMy);
    const glm::vec2 mouse2D(dx, -dy);

    float t = 0.0f;

    if (m_mode == Mode::Uniform)
    {
        // Center handle: simple vertical drag feels best.
        t = -dy; // drag up => positive
    }
    else
    {
        // Axis handles: project the axis onto screen using viewport basis.
        const glm::vec3 dir = axisDir(m_mode);

        const float ax = glm::dot(dir, vp->rightDirection());
        const float ay = glm::dot(dir, vp->upDirection());

        glm::vec2 axis2D(ax, ay);

        const float len2 = axis2D.x * axis2D.x + axis2D.y * axis2D.y;

        if (len2 < 1e-8f)
        {
            // Axis points toward/away from camera (no clear screen direction).
            // Fall back to vertical drag.
            t = -dy;
        }
        else
        {
            axis2D /= std::sqrt(len2);

            // Drag "along the axis on screen" increases scale.
            t = glm::dot(mouse2D, axis2D);
        }
    }

    const float factor = std::pow(2.0f, t * k);

    const float s0 = std::max(0.0001f, m_startScale.x);
    const float s  = glm::clamp(s0 * factor, 0.0001f, 10000.0f);

    *m_scale = glm::vec3{s, s, s};
}

void ScaleGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void ScaleGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_scale)
        return;

    if (!m_dragging)
        m_origin = sel::selection_center_bounds(scene);

    const glm::vec3 origin = m_origin;

    const float px = vp->pixelScale(origin);

    // Pixel-tuned sizes (world units at pivot).
    m_centerHalfWorld  = std::max(0.0001f, px * 10.0f); // ~20px square
    m_axisLenWorld     = std::max(0.05f, px * 70.0f);   // ~70px stem
    m_axisBoxHalfWorld = std::max(0.0001f, px * 7.0f);  // ~14px tip square

    m_overlayHandler.clear();

    // Center handle (3)
    {
        m_overlayHandler.begin_overlay(static_cast<int32_t>(Mode::Uniform));

        const float pickHalf = m_centerHalfWorld * 1.35f;

        buildBillboardSquare(vp, origin, pickHalf, glm::vec4{1, 1, 1, 0.2f}, true);
        buildBillboardSquare(vp, origin, m_centerHalfWorld, glm::vec4{1, 1, 1, 1}, false);

        m_overlayHandler.set_axis(glm::vec3{0.0f});
        m_overlayHandler.end_overlay();
    }

    // Axis stems + billboard tips (0/1/2)
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
