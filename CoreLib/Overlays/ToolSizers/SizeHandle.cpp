#include "SizeHandle.hpp"

#include <glm/gtx/component_wise.hpp>

#include "OverlayHandler.hpp"
#include "Viewport.hpp"

SizeHandle::SizeHandle(glm::ivec3 direction, glm::vec3* min, glm::vec3* max) : m_dir(direction), m_min(min), m_max(max)
{
}

void SizeHandle::beginDrag(Viewport* /*vp*/, float /*x*/, float /*y*/)
{
}

void SizeHandle::drag(Viewport* vp, float x, float y)
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

    if (m_dir == glm::ivec3(0, 0, 0))
    {
        // Move BOTH min and max by the drag delta
        glm::vec3 c     = center();
        glm::vec3 delta = pt - c;
        *m_min += delta;
        *m_max += delta;
    }
    else
    {
        for (int i = 0; i < 3; ++i)
        {
            if (m_dir[i] > 0)
                (*m_max)[i] = pt[i];
            else if (m_dir[i] < 0)
                (*m_min)[i] = pt[i];
        }
    }

    *m_min = un::roundToPrecision(*m_min, 4);
    *m_max = un::roundToPrecision(*m_max, 4);
}

void SizeHandle::endDrag(Viewport* /*vp*/, float /*x*/, float /*y*/)
{
}

void SizeHandle::construct(Viewport* vp, OverlayHandler& overlayHandler)
{
    glm::vec3 p = this->position();

    // // Center handle: draw as big point
    // if (m_dir == glm::ivec3(0, 0, 0))
    // {
    //     overlayHandler.add_point(p, 16.0f, glm::vec4(0.98f, 0.98f, 0.02f, 1.0f));
    //     return;
    // }

    // // Axis handles: draw as large magenta points
    // int numNonZero = (m_dir.x != 0) + (m_dir.y != 0) + (m_dir.z != 0);
    // if (numNonZero == 1)
    // {
    //     overlayHandler.add_point(p, 12.0f, glm::vec4(0.85f, 0.2f, 0.7f, 1.0f));
    //     return;
    // }

    // For now just render as lines - uncomment top and remove this if can render points
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

        // -----------------------------------------------------------------
        // Pure axis handle (face/edge) – simple “T” shape
        // -----------------------------------------------------------------
        // if (numNonZero == 1)
        // {
        //     glm::vec4 col(0.85f, 0.2f, 0.7f, 1.0f); // magenta-ish
        //     float     thick = 1.5f;

        //     glm::vec3 axis(0.0f);
        //     if (dir.x != 0)
        //         axis.x = (dir.x > 0 ? 1.f : -1.f);
        //     if (dir.y != 0)
        //         axis.y = (dir.y > 0 ? 1.f : -1.f);
        //     if (dir.z != 0)
        //         axis.z = (dir.z > 0 ? 1.f : -1.f);

        //     glm::vec3 mainDir = glm::normalize(axis);
        //     glm::vec3 sideDir = glm::normalize(glm::cross(mainDir, vp->viewDirection()));

        //     glm::vec3 p1 = pOut + mainDir * s;
        //     glm::vec3 p2 = pOut - mainDir * s * 0.5f;
        //     glm::vec3 s1 = pOut + sideDir * s * 0.5f;
        //     glm::vec3 s2 = pOut - sideDir * s * 0.5f;

        //     overlayHandler.add_line(p2, p1, thick, col); // stem
        //     overlayHandler.add_line(s1, s2, thick, col); // cross bar
        //     // return;
        // }
    }

    // All other handles: draw as lines
    glm::vec4 color(0.02f, 0.72f, 0.98f, 1.0f);
    float     thickness = 2.0f;
    float     len       = 0.09f;

    for (int i = 0; i < 3; i++)
    {
        if (!un::is_zero(glm::abs(size()[i])))
        {
            glm::vec3 line(0.f);
            line[i] = (p[i] > center()[i] ? -len : len);

            if (m_dir[i] != 0)
                overlayHandler.add_line(p, p + line, thickness, color);
            else
                overlayHandler.add_line(p + line, p - line, thickness, color);
        }
    }
}

glm::vec3 SizeHandle::position() const
{
    glm::vec3 c = center();
    glm::vec3 s = size();
    glm::vec3 pos;
    pos.x = c.x + (m_dir.x * s.x / 2.f);
    pos.y = c.y + (m_dir.y * s.y / 2.f);
    pos.z = c.z + (m_dir.z * s.z / 2.f);
    return pos;
}

glm::ivec3 SizeHandle::axis() const
{
    return m_dir;
}

glm::vec3 SizeHandle::center() const
{
    return (*m_min + *m_max) * 0.5f;
}

glm::vec3 SizeHandle::size() const
{
    return (*m_max - *m_min);
}
