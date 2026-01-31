#include "RotateGizmo.hpp"

#include <algorithm>
#include <cmath>

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

static constexpr float kPi = 3.14159265358979323846f;

RotateGizmo::RotateGizmo(glm::vec3* amountDeg) : m_amountDeg(amountDeg)
{
    if (m_amountDeg)
        *m_amountDeg = glm::vec3(0.0f);
}

glm::vec3 RotateGizmo::axisDir(Axis a) noexcept
{
    switch (a)
    {
        case Axis::X:
            return glm::vec3(1.0f, 0.0f, 0.0f);
        case Axis::Y:
            return glm::vec3(0.0f, 1.0f, 0.0f);
        case Axis::Z:
            return glm::vec3(0.0f, 0.0f, 1.0f);
        default:
            return glm::vec3(0.0f);
    }
}

void RotateGizmo::buildOrthonormalBasis(const glm::vec3& n, glm::vec3& outU, glm::vec3& outV) noexcept
{
    // Pick a helper not parallel to n
    glm::vec3 h = (std::abs(n.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);

    outU = glm::cross(h, n);
    if (un::is_zero(outU))
        outU = glm::vec3(1, 0, 0);

    outU = un::safe_normalize(outU);
    outV = un::safe_normalize(glm::cross(n, outU));
}

bool RotateGizmo::ringPlaneHit(Viewport*        vp,
                               const glm::vec3& origin,
                               const glm::vec3& axisN,
                               float            mx,
                               float            my,
                               glm::vec3&       outHit) const noexcept
{
    // Intersect with the ring plane (plane through origin, normal = axisN)
    return vp->rayPlaneHit(mx, my, origin, axisN, outHit);
}

float RotateGizmo::signedAngleOnPlane(const glm::vec3& axisN,
                                      const glm::vec3& fromUnit,
                                      const glm::vec3& toUnit) const noexcept
{
    // Signed angle around axisN between two unit vectors in the plane.
    const float c = glm::clamp(glm::dot(fromUnit, toUnit), -1.0f, 1.0f);
    const float s = glm::dot(axisN, glm::cross(fromUnit, toUnit));
    return std::atan2(s, c);
}

void RotateGizmo::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amountDeg)
        return;

    const int32_t h = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_axis          = axisFromHandle(h);
    m_dragging      = (m_axis != Axis::None);

    if (!m_dragging)
        return;

    m_startAmount = *m_amountDeg;

    // Pivot: selection center. (No subtraction like translate; rotation keeps pivot stable.)
    m_origin = sel::selection_center_bounds(scene);

    const glm::vec3 axisN = axisDir(m_axis);

    glm::vec3 hit{};
    if (!ringPlaneHit(vp, m_origin, axisN, ev.x, ev.y, hit))
    {
        // If we can't hit (parallel ray), just cancel drag.
        m_dragging = false;
        m_axis     = Axis::None;
        return;
    }

    glm::vec3 v = hit - m_origin;
    if (un::is_zero(v))
        v = glm::vec3(1, 0, 0);

    // Project to plane (should already be on plane, but keep robust)
    v -= axisN * glm::dot(v, axisN);
    m_startDir = un::safe_normalize(v, glm::vec3(1, 0, 0));
}

void RotateGizmo::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    if (!vp || !scene || !m_amountDeg)
        return;

    if (!m_dragging || m_axis == Axis::None)
        return;

    const glm::vec3 axisN = axisDir(m_axis);

    glm::vec3 hit{};
    if (!ringPlaneHit(vp, m_origin, axisN, ev.x, ev.y, hit))
        return;

    glm::vec3 v = hit - m_origin;
    v -= axisN * glm::dot(v, axisN);

    if (un::is_zero(v))
        return;

    const glm::vec3 curDir = un::safe_normalize(v);

    float angRad = signedAngleOnPlane(axisN, m_startDir, curDir);
    float angDeg = angRad * (180.0f / kPi);

    // Optional: snap with Shift (15 degrees)
    if (ev.shift_key)
    {
        constexpr float snap = 15.0f;
        angDeg               = std::round(angDeg / snap) * snap;
    }

    glm::vec3 out = m_startAmount;

    switch (m_axis)
    {
        case Axis::X:
            out.x = m_startAmount.x + angDeg;
            break;
        case Axis::Y:
            out.y = m_startAmount.y + angDeg;
            break;
        case Axis::Z:
            out.z = m_startAmount.z + angDeg;
            break;
        default:
            break;
    }

    *m_amountDeg = out;
}

void RotateGizmo::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
    m_dragging = false;
    m_axis     = Axis::None;
}

void RotateGizmo::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene || !m_amountDeg)
        return;

    // Pivot follows selection when not dragging
    if (!m_dragging)
        m_origin = sel::selection_center_bounds(scene);

    // Screen-sized ring radius in world units
    const float px = vp->pixelScale(m_origin);
    m_radiusW      = std::max(0.01f, px * 90.0f);

    m_overlayHandler.clear();

    auto addRing = [&](int32_t handle, const glm::vec3& axisN, const glm::vec4& color) {
        m_overlayHandler.begin_overlay(handle);

        // Basis in ring plane
        glm::vec3 u{}, v{};
        buildOrthonormalBasis(axisN, u, v);

        // Circle segments
        constexpr int kSegs = 64;
        const float   step  = (2.0f * kPi) / static_cast<float>(kSegs);

        glm::vec3 prev = m_origin + (u * m_radiusW);

        for (int i = 1; i <= kSegs; ++i)
        {
            const float     a = step * static_cast<float>(i);
            const glm::vec3 p = m_origin + (u * std::cos(a) + v * std::sin(a)) * m_radiusW;

            m_overlayHandler.add_line(prev, p, 3.0f, color);
            prev = p;
        }

        // Helps the old colinear fallback if it still exists;
        // harmless even if we remove that later.
        m_overlayHandler.set_axis(axisN);
        m_overlayHandler.end_overlay();
    };

    addRing(0, glm::vec3(1, 0, 0), glm::vec4(1, 0.2f, 0.2f, 1));
    addRing(1, glm::vec3(0, 1, 0), glm::vec4(0.2f, 1, 0.2f, 1));
    addRing(2, glm::vec3(0, 0, 1), glm::vec4(0.2f, 0.6f, 1, 1));
}
