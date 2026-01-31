//=============================================================================
// PrimitiveGizmo.cpp
//=============================================================================
#include "PrimitiveGizmo.hpp"

#include <algorithm>
#include <glm/gtx/norm.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

PrimitiveGizmo::PrimitiveGizmo(glm::vec3* center, glm::vec3* size) : m_center(center), m_size(size)
{
}

glm::vec3 PrimitiveGizmo::safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
{
    if (glm::length2(v) < 1e-12f)
        return fallback;
    return glm::normalize(v);
}

glm::vec3 PrimitiveGizmo::axisDir(Mode m) noexcept
{
    switch (m)
    {
        case Mode::PosX:
            return glm::vec3{1.0f, 0.0f, 0.0f};
        case Mode::NegX:
            return glm::vec3{-1.0f, 0.0f, 0.0f};
        case Mode::PosY:
            return glm::vec3{0.0f, 1.0f, 0.0f};
        case Mode::NegY:
            return glm::vec3{0.0f, -1.0f, 0.0f};
        case Mode::PosZ:
            return glm::vec3{0.0f, 0.0f, 1.0f};
        case Mode::NegZ:
            return glm::vec3{0.0f, 0.0f, -1.0f};
        default:
            return glm::vec3{0.0f};
    }
}

int32_t PrimitiveGizmo::axisIndex(Mode m) noexcept
{
    switch (m)
    {
        case Mode::PosX:
        case Mode::NegX:
            return 0;
        case Mode::PosY:
        case Mode::NegY:
            return 1;
        case Mode::PosZ:
        case Mode::NegZ:
            return 2;
        default:
            return -1;
    }
}

glm::vec3 PrimitiveGizmo::dragPointOnAxisPlane(Viewport*        vp,
                                               const glm::vec3& origin,
                                               const glm::vec3& axisDirIn,
                                               float            mx,
                                               float            my) const
{
    const glm::vec3 aDir = safeNormalize(axisDirIn, glm::vec3{1.0f, 0.0f, 0.0f});

    // Plane containing the axis and facing the camera as much as possible.
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

    n = safeNormalize(glm::cross(aDir, n), glm::vec3{0.0f, 0.0f, 1.0f});

    glm::vec3 hit = glm::vec3{0.0f};
    if (vp->rayPlaneHit(mx, my, origin, n, hit))
        return hit;

    return origin;
}

void PrimitiveGizmo::buildBillboardSquare(Viewport*        vp,
                                          const glm::vec3& center,
                                          float            halfExtentWorld,
                                          const glm::vec4& color,
                                          bool             filledForPick)
{
    // Uses frozen basis during drag to prevent sign-flip flicker.
    glm::vec3 r = glm::vec3{1.0f, 0.0f, 0.0f};
    glm::vec3 u = glm::vec3{0.0f, 1.0f, 0.0f};

    if (m_dragging)
    {
        r = m_bbRight;
        u = m_bbUp;
    }
    else
    {
        r = vp->rightDirection();
        u = vp->upDirection();
    }

    r = safeNormalize(r, glm::vec3{1.0f, 0.0f, 0.0f});
    u = safeNormalize(u, glm::vec3{0.0f, 1.0f, 0.0f});

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

void PrimitiveGizmo::applyFaceDragDelta(float delta)
{
    glm::vec3 newCenter = m_startCenter;
    glm::vec3 newSize   = m_startSize;

    if (m_axisIdx < 0)
        return;

    float* sizeComp = nullptr;
    if (m_axisIdx == 0)
        sizeComp = &newSize.x;
    else if (m_axisIdx == 1)
        sizeComp = &newSize.y;
    else
        sizeComp = &newSize.z;

    float desired = (*sizeComp) + delta;

    if (desired < m_minSize)
    {
        delta   = m_minSize - (*sizeComp);
        desired = m_minSize;
    }

    *sizeComp = desired;

    newCenter += m_axis * (delta * 0.5f);

    *m_center = newCenter;
    *m_size   = newSize;
}

void PrimitiveGizmo::mouseDown(Viewport* vp, Scene*, const CoreEvent& ev)
{
    if (!vp || !m_center || !m_size)
        return;

    const int32_t handle = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_mode               = modeFromHandle(handle);
    m_dragging           = (m_mode != Mode::None);

    if (!m_dragging)
        return;

    // Freezes billboard basis for this drag (prevents flicker).
    m_bbRight = safeNormalize(vp->rightDirection(), glm::vec3{1.0f, 0.0f, 0.0f});
    m_bbUp    = safeNormalize(vp->upDirection(), glm::vec3{0.0f, 1.0f, 0.0f});

    m_startCenter = *m_center;
    m_startSize   = *m_size;

    m_startMx = ev.x;
    m_startMy = ev.y;

    if (m_mode == Mode::Center)
        return;

    m_axis    = axisDir(m_mode);
    m_axisIdx = axisIndex(m_mode);

    const float     half       = std::max(m_minSize, m_startSize[m_axisIdx]) * 0.5f;
    const glm::vec3 faceCenter = m_startCenter + m_axis * half;

    const glm::vec3 hit0 = dragPointOnAxisPlane(vp, faceCenter, m_axis, ev.x, ev.y);
    m_startParam         = glm::dot(hit0 - m_startCenter, m_axis);
}

void PrimitiveGizmo::mouseDrag(Viewport* vp, Scene*, const CoreEvent& ev)
{
    if (!vp || !m_center || !m_size)
        return;

    if (!m_dragging || m_mode == Mode::None)
        return;

    if (m_mode == Mode::Center)
    {
        const float dx = (ev.x - m_startMx);
        const float dy = (ev.y - m_startMy);

        const float px = vp->pixelScale(m_startCenter);

        const glm::vec3 worldDelta =
            vp->rightDirection() * (dx * px) +
            vp->upDirection() * (-dy * px);

        *m_center = (m_startCenter + worldDelta);
        return;
    }

    const float     half        = std::max(m_minSize, m_startSize[m_axisIdx]) * 0.5f;
    const glm::vec3 faceCenter0 = m_startCenter + m_axis * half;

    const glm::vec3 hit      = dragPointOnAxisPlane(vp, faceCenter0, m_axis, ev.x, ev.y);
    const float     curParam = glm::dot(hit - m_startCenter, m_axis);

    const float delta = (curParam - m_startParam);
    applyFaceDragDelta(delta);
}

void PrimitiveGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_mode     = Mode::None;
    m_dragging = false;
}

void PrimitiveGizmo::render(Viewport* vp, Scene*)
{
    if (!vp || !m_center || !m_size)
        return;

    const glm::vec3 c = *m_center;
    const glm::vec3 s = *m_size;

    const float px = vp->pixelScale(c);

    m_centerHalfWorld = std::max(0.0001f, px * 10.0f);
    m_axisLenWorld    = std::max(0.05f, px * 70.0f);
    m_tipRadiusWorld  = std::max(0.0001f, px * 7.0f);

    m_overlayHandler.clear();

    // Camera-facing normal for billboarded tip disks.
    const glm::vec3 right = m_dragging ? m_bbRight : vp->rightDirection();
    const glm::vec3 up    = m_dragging ? m_bbUp : vp->upDirection();
    const glm::vec3 faceN = safeNormalize(glm::cross(right, up), glm::vec3{0.0f, 0.0f, 1.0f});

    // Center handle
    {
        m_overlayHandler.begin_overlay(static_cast<int32_t>(Mode::Center));

        const float pickHalf = m_centerHalfWorld * 1.35f;
        buildBillboardSquare(vp, c, pickHalf, glm::vec4{1, 1, 1, 0.2f}, true);
        buildBillboardSquare(vp, c, m_centerHalfWorld, glm::vec4{1, 1, 1, 1.0f}, false);

        m_overlayHandler.set_axis(glm::vec3{0.0f});
        m_overlayHandler.end_overlay();
    }

    auto addFace = [&](Mode mode, const glm::vec3& axis, float halfExtent, const glm::vec4& color) {
        const int32_t h = static_cast<int32_t>(mode);

        const glm::vec3 faceCenter = c + axis * halfExtent;
        const glm::vec3 stemA      = faceCenter;
        const glm::vec3 stemB      = faceCenter + axis * m_axisLenWorld;

        m_overlayHandler.begin_overlay(h);

        // Gizmo stem thickness matches the other gizmos (NormalPull/Translate/Stretch).
        m_overlayHandler.add_line(stemA, stemB, 4.0f, color);

        // Tip is a camera-facing filled disk. The polygon interior is used for picking.
        m_overlayHandler.set_axis(faceN);
        m_overlayHandler.add_filled_circle(stemB,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 0.25f},
                                           2.5f,
                                           48);

        // Crisp cap pass.
        m_overlayHandler.add_filled_circle(stemB,
                                           m_tipRadiusWorld,
                                           glm::vec4{color.r, color.g, color.b, 1.0f},
                                           2.5f,
                                           48);

        // Axis hint remains the actual face axis for tool logic / constraints.
        m_overlayHandler.set_axis(axis);
        m_overlayHandler.end_overlay();
    };

    const float hx = std::max(m_minSize, s.x) * 0.5f;
    const float hy = std::max(m_minSize, s.y) * 0.5f;
    const float hz = std::max(m_minSize, s.z) * 0.5f;

    addFace(Mode::PosX, glm::vec3{1, 0, 0}, hx, glm::vec4{1, 0, 0, 1});
    addFace(Mode::NegX, glm::vec3{-1, 0, 0}, hx, glm::vec4{1, 0, 0, 1});

    addFace(Mode::PosY, glm::vec3{0, 1, 0}, hy, glm::vec4{0, 1, 0, 1});
    addFace(Mode::NegY, glm::vec3{0, -1, 0}, hy, glm::vec4{0, 1, 0, 1});

    addFace(Mode::PosZ, glm::vec3{0, 0, 1}, hz, glm::vec4{0, 0, 1, 1});
    addFace(Mode::NegZ, glm::vec3{0, 0, -1}, hz, glm::vec4{0, 0, 1, 1});
}
