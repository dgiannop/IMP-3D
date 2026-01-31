//=============================================================================
// NormalPullGizmo.cpp
//=============================================================================
#include "NormalPullGizmo.hpp"

#include <algorithm>
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

    // Plane containing axis and facing camera as much as possible.
    const glm::vec3 camPos  = vp->cameraPosition();
    glm::vec3       viewDir = origin - camPos;
    viewDir                 = safeNormalize(viewDir, glm::vec3{0.0f, 0.0f, -1.0f});

    glm::vec3 n = glm::cross(aDir, viewDir);

    // Degenerate fallback when axis aligns with view direction.
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

void NormalPullGizmo::buildBillboardSquare(Viewport*        vp,
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

void NormalPullGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amount)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_dragging           = (handle == kHandle);

    if (!m_dragging)
        return;

    m_startAmount = *m_amount;

    // Drag anchor stays fixed during the drag for stability.
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

    // Compute amount relative to fixed drag anchor (m_origin).
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

    // Pixel-tuned sizes (world units at pivot).
    m_axisLenWorld = std::max(0.05f, px * 85.0f);  // ~85px stem
    m_tipHalfWorld = std::max(0.0001f, px * 7.0f); // ~14px tip

    m_overlayHandler.clear();

    m_overlayHandler.begin_overlay(kHandle);

    // Visual follow: while dragging, the gizmo base moves with the preview amount
    // so it appears attached to the extruded surface.
    const glm::vec3 base = m_dragging ? (m_origin + m_axis * (*m_amount)) : m_origin;

    const glm::vec3 stemA = base;
    const glm::vec3 stemB = base + m_axis * m_axisLenWorld;

    // Stem
    m_overlayHandler.add_line(stemA, stemB, 8.0f, glm::vec4{1, 1, 1, 1});

    // Tip (billboard square)
    buildBillboardSquare(vp, stemB, m_tipHalfWorld, glm::vec4{1, 1, 1, 0.25f}, true);
    buildBillboardSquare(vp, stemB, m_tipHalfWorld, glm::vec4{1, 1, 1, 1}, false);

    m_overlayHandler.set_axis(m_axis);
    m_overlayHandler.end_overlay();
}
