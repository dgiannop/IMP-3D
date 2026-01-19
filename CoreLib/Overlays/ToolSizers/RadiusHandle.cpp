#include "RadiusHandle.hpp"

#include <glm/gtx/component_wise.hpp>

#include "OverlayHandler.hpp"
#include "Viewport.hpp"

RadiusHandle::RadiusHandle(glm::ivec3 direction, glm::vec3* radius, glm::vec3* center) : m_dir(direction), m_radius(radius), m_center(center)
{
}

void RadiusHandle::beginDrag(Viewport* /*vp*/, float /*x*/, float /*y*/)
{
}

void RadiusHandle::drag(Viewport* vp, float x, float y)
{
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

    // -----------------------------------------------------------------
    // Center handle: move center
    // -----------------------------------------------------------------
    if (m_dir == glm::ivec3(0))
    {
        *m_center = pt;
    }
    // -----------------------------------------------------------------
    // Axis handle(s): change radius component(s)
    // -----------------------------------------------------------------
    else
    {
        if (m_dir.x != 0)
            m_radius->x = glm::abs(pt.x - m_center->x);
        if (m_dir.y != 0)
            m_radius->y = glm::abs(pt.y - m_center->y);
        if (m_dir.z != 0)
            m_radius->z = glm::abs(pt.z - m_center->z);
    }

    *m_center = un::roundToPrecision(*m_center, 4);
    *m_radius = un::roundToPrecision(*m_radius, 4);
}

void RadiusHandle::endDrag(Viewport* /*vp*/, float /*x*/, float /*y*/)
{
}

void RadiusHandle::construct(Viewport* vp, OverlayHandler& overlayHandler)
{
    glm::vec3 p = this->position();

    // For now render as lines - if/when overlay points exist, swap to points.
    {
        // Push slightly towards camera to avoid z-fighting
        glm::vec3 viewDir = vp->viewDirection();
        float     eps     = vp->pixelScale(p) * 0.5f;
        glm::vec3 pOut    = p - viewDir * eps;

        // Screen-space-ish size
        float s = vp->pixelScale(pOut) * 5.0f;

        // -----------------------------------------------------------------
        // Center handle: draw a small cross
        // -----------------------------------------------------------------
        if (m_dir == glm::ivec3(0))
        {
            glm::vec3 right = vp->rightDirection();
            glm::vec3 up    = vp->upDirection();
            glm::vec4 col   = glm::vec4(0.98f, 0.98f, 0.02f, 0.7f); // yellow

            overlayHandler.add_line(pOut - right * s, pOut + right * s, 1.5f, col);
            overlayHandler.add_line(pOut - up * s, pOut + up * s, 1.5f, col);
            return;
        }
    }

    // -----------------------------------------------------------------
    // Axis handles: draw small tick marks along active axes
    // -----------------------------------------------------------------
    glm::vec3 c         = *m_center;
    glm::vec4 color     = glm::vec4(0.02f, 0.72f, 0.98f, 1.0f);
    float     thickness = 2.0f;

    // Same “feel” as old code but in world-units; I can also make this pixel-scaled
    float len = 0.09f;

    for (int i = 0; i < 3; ++i)
    {
        if (un::is_zero((*m_radius)[i]))
            continue;

        glm::vec3 line(0.f);
        line[i] = (p[i] > c[i] ? -len : len);

        if (m_dir[i] != 0) // this handle edits this axis radius
            overlayHandler.add_line(p, p + line, thickness, color);
        else // show a symmetric tick if axis not active for this handle
            overlayHandler.add_line(p + line, p - line, thickness, color);
    }
}

glm::vec3 RadiusHandle::position() const
{
    glm::vec3 pos;
    pos.x = m_center->x + (m_dir.x * m_radius->x);
    pos.y = m_center->y + (m_dir.y * m_radius->y);
    pos.z = m_center->z + (m_dir.z * m_radius->z);
    return pos;
}

glm::ivec3 RadiusHandle::axis() const
{
    return m_dir;
}
