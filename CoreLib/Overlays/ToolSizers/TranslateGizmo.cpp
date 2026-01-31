#include "TranslateGizmo.hpp"

#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

TranslateGizmo::TranslateGizmo(glm::vec3* amount) : m_amount{amount}
{
    if (m_amount)
        *m_amount = glm::vec3{0.0f};
}

glm::vec3 TranslateGizmo::axisDir(Axis a) noexcept
{
    switch (a)
    {
        case Axis::X:
            return glm::vec3{1.0f, 0.0f, 0.0f};
        case Axis::Y:
            return glm::vec3{0.0f, 1.0f, 0.0f};
        case Axis::Z:
            return glm::vec3{0.0f, 0.0f, 1.0f};
        default:
            return glm::vec3{0.0f};
    }
}

glm::vec3 TranslateGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                               const glm::vec3& origin,
                                               Axis             axis,
                                               float            mx,
                                               float            my) const
{
    const glm::vec3 aDir = axisDir(axis);

    // Build a plane that contains the axis and faces the camera as much as possible.
    // We choose plane normal: n = normalize(cross(aDir, cross(aDir, viewDir))).
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = origin - camPos;

    if (glm::length2(viewDir) < 1e-8f)
        viewDir = glm::vec3{0.0f, 0.0f, -1.0f};
    else
        viewDir = glm::normalize(viewDir);

    glm::vec3 n = glm::cross(aDir, viewDir);
    if (glm::length2(n) < 1e-8f)
    {
        n = glm::cross(aDir, glm::vec3{0.0f, 0.0f, 1.0f});
        if (glm::length2(n) < 1e-8f)
            n = glm::cross(aDir, glm::vec3{0.0f, 1.0f, 0.0f});
    }

    // plane normal is perpendicular to axis, stable even when view aligns with axis
    n = glm::normalize(glm::cross(aDir, n));

    glm::vec3 hit{};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

void TranslateGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_axis               = axisFromHandle(handle);
    m_dragging           = (m_axis != Axis::None);

    if (!m_dragging)
        return;

    // Cache starting state (absolute parameter)
    m_startAmount = *m_amount;

    // IMPORTANT: base pivot is the selection center in the CURRENT scene state.
    // It already includes the current amount's deformation, so we must subtract it out.
    const glm::vec3 curCenter = sel::selection_center_bounds(scene);
    m_baseOrigin              = curCenter - m_startAmount;

    // Origin used for plane intersection is the pivot at the *current* amount.
    m_origin  = m_baseOrigin + m_startAmount;
    m_axisDir = axisDir(m_axis);

    m_startOnPlane = dragPointOnAxisPlane(vp, m_origin, m_axis, ev.x, ev.y);
}

void TranslateGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    if (!m_dragging || m_axis == Axis::None)
        return;

    const glm::vec3 curOnPlane = dragPointOnAxisPlane(vp, m_origin, m_axis, ev.x, ev.y);

    const glm::vec3 dPlane = curOnPlane - m_startOnPlane;
    const float     tAxis  = glm::dot(dPlane, m_axisDir);

    // Write tool parameter (absolute)
    *m_amount = m_startAmount + (m_axisDir * tAxis);
}

void TranslateGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_axis     = Axis::None;
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

    const float px = vp->pixelScale(origin);
    m_length       = std::max(0.05f, px * 80.0f);

    m_overlayHandler.clear();

    auto addAxis = [&](int32_t handle, const glm::vec3& dir, const glm::vec4& color) {
        m_overlayHandler.begin_overlay(handle);
        m_overlayHandler.add_line(origin, origin + dir * m_length, 8.f, color);
        m_overlayHandler.set_axis(dir);
        m_overlayHandler.end_overlay();
    };

    addAxis(0, glm::vec3{1, 0, 0}, glm::vec4{1, 0, 0, 1});
    addAxis(1, glm::vec3{0, 1, 0}, glm::vec4{0, 1, 0, 1});
    addAxis(2, glm::vec3{0, 0, 1}, glm::vec4{0, 0, 1, 1});
}
