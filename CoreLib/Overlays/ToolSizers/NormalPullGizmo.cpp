//=============================================================================
// NormalPullGizmo.cpp
//=============================================================================
#include "NormalPullGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

namespace
{
    constexpr int32_t kHandle = 0;
} // namespace

glm::vec3 NormalPullGizmo::safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
{
    if (glm::length2(v) < 1e-12f)
        return fallback;
    return glm::normalize(v);
}

NormalPullGizmo::NormalPullGizmo(float* amount) : m_amount(amount)
{
    if (m_amount)
        *m_amount = 0.0f;
}

glm::vec3 NormalPullGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                                const glm::vec3& origin,
                                                const glm::vec3& axisDir,
                                                float            mx,
                                                float            my) const
{
    const glm::vec3 aDir = safeNormalize(axisDir, glm::vec3{0.0f, 0.0f, 1.0f});

    // The drag plane contains the axis and is chosen to face the camera as much as possible.
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = origin - camPos;
    viewDir                 = safeNormalize(viewDir, glm::vec3{0.0f, 0.0f, -1.0f});

    glm::vec3 n = glm::cross(aDir, viewDir);

    // Fallback when the axis aligns with the view direction.
    if (glm::length2(n) < 1e-10f)
    {
        n = glm::cross(aDir, glm::vec3{0.0f, 0.0f, 1.0f});
        if (glm::length2(n) < 1e-10f)
            n = glm::cross(aDir, glm::vec3{0.0f, 1.0f, 0.0f});
    }

    n = safeNormalize(glm::cross(aDir, n), glm::vec3{0.0f, 0.0f, 1.0f}); // plane normal

    glm::vec3 hit = glm::vec3{0.0f};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

void NormalPullGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_dragging           = (handle == kHandle);

    if (!m_dragging)
        return;

    m_startAmount = *m_amount;

    m_origin = sel::selection_center_bounds(scene);
    m_axis   = safeNormalize(sel::selection_normal(scene), glm::vec3{0.0f, 0.0f, 1.0f});

    m_startHit   = dragPointOnAxisPlane(vp, m_origin, m_axis, ev.x, ev.y);
    m_startParam = glm::dot(m_startHit - m_origin, m_axis);
}

void NormalPullGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    if (!m_dragging)
        return;

    const glm::vec3 curHit   = dragPointOnAxisPlane(vp, m_origin, m_axis, ev.x, ev.y);
    const float     curParam = glm::dot(curHit - m_origin, m_axis);

    *m_amount = (m_startAmount + (curParam - m_startParam));
}

void NormalPullGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_dragging = false;
}

void NormalPullGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_amount)
        return;

    if (!m_dragging)
    {
        m_origin = sel::selection_center_bounds(scene);
        m_axis   = safeNormalize(sel::selection_normal(scene), glm::vec3{0.0f, 0.0f, 1.0f});
    }

    const float px = vp->pixelScale(m_origin);

    const float baseLen   = std::max(0.05f, px * 85.0f);  // ~85px
    const float tipRadius = std::max(0.0001f, px * 7.0f); // ~14px diameter

    float len = baseLen;
    if (m_dragging && !m_followAmountBase)
        len = baseLen + std::abs(*m_amount);

    m_axisLenWorld   = len;
    m_tipRadiusWorld = tipRadius;

    // Yellow gizmo like the reference.
    const glm::vec4 col = glm::vec4{1.0f, 1.0f, 0.0f, 1.0f};

    // Slightly translucent fill so the disk reads as a handle without being too heavy.
    const glm::vec4 fillCol = glm::vec4{1.0f, 1.0f, 0.0f, 0.85f};

    // Thinner stem to match the reference.
    const float stemThicknessPx = 5.0f;

    m_overlayHandler.clear();
    m_overlayHandler.begin_overlay(kHandle);

    const glm::vec3 base  = (m_dragging && m_followAmountBase) ? (m_origin + m_axis * (*m_amount)) : m_origin;
    const glm::vec3 stemA = base;
    const glm::vec3 stemB = base + m_axis * m_axisLenWorld;

    // Stem.
    m_overlayHandler.add_line(stemA, stemB, stemThicknessPx, col);

    // Tip disk should face the camera (billboard) to avoid the "tilted/shaded" look.
    const glm::vec3 r = vp->rightDirection();
    const glm::vec3 u = vp->upDirection();
    const glm::vec3 n = safeNormalize(glm::cross(r, u), glm::vec3{0.0f, 0.0f, 1.0f});

    // The overlay axis hint is used as the circle plane normal by add_filled_circle().
    m_overlayHandler.set_axis(n);

    // Filled disk (also serves as the pick target via polygon interior hit-test).
    m_overlayHandler.add_filled_circle(stemB, m_tipRadiusWorld, fillCol, 2.0f, 48);

    // Optional crisp outline. If polygon outlines are not emitted by the renderer yet,
    // this still leaves a visible filled disk (the important part).
    m_overlayHandler.add_filled_circle(stemB, m_tipRadiusWorld, col, 2.0f, 48);

    m_overlayHandler.end_overlay();
}
