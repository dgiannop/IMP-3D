#include "RadiusSizer.hpp"

#include "OverlayHandler.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

RadiusSizer::RadiusSizer(glm::vec3* radius, glm::vec3* center) : m_radius(radius), m_center(center)
{
    m_handles.emplace_back(glm::ivec3{+1, 0, 0}, m_radius, m_center);
    m_handles.emplace_back(glm::ivec3{-1, 0, 0}, m_radius, m_center);
    m_handles.emplace_back(glm::ivec3{0, +1, 0}, m_radius, m_center);
    m_handles.emplace_back(glm::ivec3{0, -1, 0}, m_radius, m_center);
    m_handles.emplace_back(glm::ivec3{0, 0, +1}, m_radius, m_center);
    m_handles.emplace_back(glm::ivec3{0, 0, -1}, m_radius, m_center);

    // Center handle last
    m_handles.emplace_back(glm::ivec3{0, 0, 0}, m_radius, m_center);
}

// int32_t RadiusSizer::pickHandle(Viewport* vp, Scene* /*scene*/, float x, float y)
// {
//     const float maxDistPx = 14.0f;

//     int32_t best   = -1;
//     float   bestD2 = maxDistPx * maxDistPx;

//     for (int32_t i = 0; i < (int32_t)m_handles.size(); ++i)
//     {
//         glm::vec3 hp = m_handles[i].position();
//         glm::vec3 sp = vp->project(hp);
//         float     dx = sp.x - x;
//         float     dy = sp.y - y;
//         float     d2 = dx * dx + dy * dy;

//         if (d2 < bestD2)
//         {
//             bestD2 = d2;
//             best   = i;
//         }
//     }
//     return best;
// }

void RadiusSizer::mouseDown(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    // Old behavior: if radius == 0, snap center under cursor and start dragging a radius handle
    if (m_radius && m_center && un::is_zero(*m_radius))
    {
        glm::vec3 pt = vp->project(*m_center);
        pt.x         = ev.x;
        pt.y         = ev.y;
        *m_center    = vp->unproject(pt);

        // pick an axis handle to "start" with (same behavior as old code setting m_curHandle=0)
        m_curHandle = 0;
    }
    else
    {
        // Pick handle via overlay handler (Option A screen-distance picking)
        std::string name = m_overlayHandler.pick(vp, ev.x, ev.y);
        m_curHandle      = (name != "") ? std::atoi(name.c_str()) : -1;
    }

    if (m_curHandle != -1)
        m_handles[m_curHandle].beginDrag(vp, ev.x, ev.y);
}

void RadiusSizer::mouseDrag(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (m_curHandle != -1)
    {
        m_handles[m_curHandle].drag(vp, ev.x, ev.y);

        // Shift = uniform radius (old behavior)
        if (ev.shift_key && m_radius)
        {
            m_radius->y = m_radius->z = m_radius->x;
        }
    }
}

void RadiusSizer::mouseUp(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (m_curHandle != -1)
    {
        m_handles[m_curHandle].endDrag(vp, ev.x, ev.y);
        m_curHandle = -1;
    }
}

void RadiusSizer::render(Viewport* vp, Scene* /*scene*/)
{
    m_overlayHandler.clear();

    for (size_t i = 0; i < m_handles.size(); ++i)
    {
        m_overlayHandler.begin_overlay(std::to_string(i));
        m_handles[i].construct(vp, m_overlayHandler);
        m_overlayHandler.set_axis(m_handles[i].axis());
        m_overlayHandler.end_overlay();
    }

    // In Vulkan path, render() is a no-op; Renderer uses overlayHandler() directly.
    // m_overlayHandler.render(vp);
}
