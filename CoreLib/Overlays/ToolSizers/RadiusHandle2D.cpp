#include "RadiusHandle2D.hpp"

#include <algorithm>
#include <glm/gtx/component_wise.hpp>

#include "OverlayHandler.hpp"
#include "Viewport.hpp"

RadiusHandle2D::RadiusHandle2D(glm::ivec3 direction, float* radius, float* height, glm::vec3* center, glm::ivec3* axis) :
    m_dir(direction),
    m_radius(radius),
    m_height(height),
    m_center(center),
    m_axis(axis)
{
}

void RadiusHandle2D::beginDrag(Viewport* /*vp*/, float /*x*/, float /*y*/)
{
}

void RadiusHandle2D::drag(Viewport* vp, float x, float y)
{
    if (!vp || !m_center || !m_axis)
        return;

    glm::vec3 pos = this->position();

    glm::vec3 pt = vp->project(pos);
    pt.x         = x;
    pt.y         = y;
    pt           = vp->unproject(pt);

    constexpr float grid        = 0.1f;
    bool            snapEnabled = true;
    if (snapEnabled)
        pt = un::snapToGrid(pt, grid);
    else
        pt = un::snapToGrid(pt, grid * 0.05f);

    // Axis basis from m_axis (major axis)
    glm::vec3 up = glm::vec3(*m_axis);
    if (un::is_zero(up))
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    up = un::safe_normalize(up);

    glm::vec3 helper = (std::abs(up.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);

    glm::vec3 right = glm::cross(helper, up);
    if (un::is_zero(right))
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    right = un::safe_normalize(right);

    // -----------------------------------------------------------------
    // Center handle: move center (same as old)
    // -----------------------------------------------------------------
    if (m_dir == glm::ivec3(0))
    {
        *m_center = pt;
    }
    // -----------------------------------------------------------------
    // Radius handle: distance in the plane perpendicular to axis
    // -----------------------------------------------------------------
    else if (m_dir.x != 0)
    {
        if (!m_radius)
            return;

        // Project onto plane through center perpendicular to up:
        glm::vec3 v = pt - *m_center;
        v -= up * glm::dot(v, up); // remove axial component

        // Scalar radius is the length of this planar vector
        *m_radius = std::max(0.0f, glm::length(v));
        *m_radius = un::roundToPrecision(*m_radius, 4);
    }
    // -----------------------------------------------------------------
    // Height handle: axial distance (full height)
    // -----------------------------------------------------------------
    else if (m_dir.y != 0)
    {
        if (!m_height)
            return;

        // Height is 2 * signed distance from center along axis.
        // (handle sits at +up * (height/2))
        float half = glm::dot(pt - *m_center, up);
        *m_height  = std::max(0.0f, std::abs(half) * 2.0f);
        *m_height  = un::roundToPrecision(*m_height, 4);
    }

    *m_center = un::roundToPrecision(*m_center, 4);
}

void RadiusHandle2D::endDrag(Viewport* /*vp*/, float /*x*/, float /*y*/)
{
}

static void drawBallCross(Viewport* vp, OverlayHandler& oh, const glm::vec3& p, float radiusPx, const glm::vec4& col)
{
    const glm::vec3 viewDir = vp->viewDirection();
    const float     eps     = vp->pixelScale(p) * 0.5f;
    const glm::vec3 pOut    = p - viewDir * eps;

    const float     s        = vp->pixelScale(pOut) * radiusPx;
    const glm::vec3 rightDir = vp->rightDirection();
    const glm::vec3 upDir    = vp->upDirection();

    oh.add_line(pOut - rightDir * s, pOut + rightDir * s, 2.0f, col);
    oh.add_line(pOut - upDir * s, pOut + upDir * s, 2.0f, col);
}

void RadiusHandle2D::construct(Viewport* vp, OverlayHandler& overlayHandler)
{
    if (!vp || !m_center || !m_axis)
        return;

    // Basis
    glm::vec3 up = glm::vec3(*m_axis);
    if (un::is_zero(up))
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    up = un::safe_normalize(up);

    glm::vec3 helper = (std::abs(up.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);

    glm::vec3 right = glm::cross(helper, up);
    if (un::is_zero(right))
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    right = un::safe_normalize(right);

    // Decide which handle this is
    glm::vec3 dir(0.0f);
    glm::vec3 p0 = *m_center;

    // Center handle: just draw the ball/cross at center and return
    if (m_dir == glm::ivec3(0))
    {
        drawBallCross(vp, overlayHandler, p0, 6.0f, glm::vec4(1.f, 0.1f, 0.1f, 1.f));
        return;
    }

    // Radius handle: anchor on surface, point outward
    if (m_dir.x != 0 && m_radius)
    {
        dir = (m_dir.x > 0) ? right : -right;
        p0  = *m_center + dir * (*m_radius);
    }

    // Height handle: anchor on top surface, point outward
    if (m_dir.y != 0 && m_height)
    {
        dir = (m_dir.y > 0) ? up : -up;
        p0  = *m_center + dir * ((*m_height) * 0.5f);
    }

    if (un::is_zero(dir))
        return;

    // Push slightly towards camera to avoid z-fighting
    glm::vec3 viewDir = vp->viewDirection();
    float     eps     = vp->pixelScale(p0) * 0.5f;
    glm::vec3 pOut    = p0 - viewDir * eps;

    // Constant screen length
    const float     lenW = vp->pixelScale(pOut) * 35.0f;
    const glm::vec3 p1   = pOut + dir * lenW;

    // Line outward + red ball
    overlayHandler.add_line(pOut, p1, 2.5f, glm::vec4(0.1f, 0.7f, 1.0f, 1.0f));
    drawBallCross(vp, overlayHandler, p1, 6.0f, glm::vec4(1.f, 0.1f, 0.1f, 1.f));
}

glm::vec3 RadiusHandle2D::position() const
{
    if (!m_center || !m_axis)
        return glm::vec3(0.0f);

    glm::vec3 up = glm::vec3(*m_axis);
    if (un::is_zero(up))
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    up = un::safe_normalize(up);

    glm::vec3 helper = (std::abs(up.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);

    glm::vec3 right = glm::cross(helper, up);
    if (un::is_zero(right))
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    right = un::safe_normalize(right);

    glm::vec3 pos = *m_center;

    // radius handle
    if (m_dir.x != 0 && m_radius)
        pos += right * (*m_radius);

    // height handle (top)
    if (m_dir.y != 0 && m_height)
        pos += up * (*m_height * 0.5f);

    return pos;
}

glm::ivec3 RadiusHandle2D::axis() const
{
    return m_dir;
}
