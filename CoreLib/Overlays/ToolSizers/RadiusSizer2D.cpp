#include "RadiusSizer2D.hpp"

#include "OverlayHandler.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

RadiusSizer2D::RadiusSizer2D(float* radius, float* height, glm::vec3* center, glm::ivec3* axis) :
    m_radius(radius),
    m_height(height),
    m_center(center),
    m_axis(axis)
{
    // 0: radius handle
    m_handles.emplace_back(glm::ivec3{1, 0, 0}, m_radius, m_height, m_center, m_axis);

    // 1: height handle
    m_handles.emplace_back(glm::ivec3{0, 1, 0}, m_radius, m_height, m_center, m_axis);

    // 2: center handle (optional but useful, matches old RadiusSizer)
    m_handles.emplace_back(glm::ivec3{0, 0, 0}, m_radius, m_height, m_center, m_axis);
}

void RadiusSizer2D::mouseDown(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (!vp || !m_center || !m_axis || !m_radius || !m_height)
        return;

    // Old behavior: if both are 0, snap center under cursor and start radius drag
    if (un::is_zero(*m_radius) && un::is_zero(*m_height))
    {
        glm::vec3 pt = vp->project(*m_center);
        pt.x         = ev.x;
        pt.y         = ev.y;
        *m_center    = vp->unproject(pt);

        m_curHandle = 0;
    }
    else
    {
        // Pick handle via overlay handler (now returns int handle directly; -1 means none)
        m_curHandle = m_overlayHandler.pick(vp, ev.x, ev.y);
    }

    if (m_curHandle != -1)
        m_handles[m_curHandle].beginDrag(vp, ev.x, ev.y);
}

void RadiusSizer2D::mouseDrag(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (!vp)
        return;

    if (m_curHandle != -1)
    {
        m_handles[m_curHandle].drag(vp, ev.x, ev.y);

        // No Shift coupling for cylinder (radius/height are independent).
        // If you later want Shift=uniform scale (both), do it here.
    }
}

void RadiusSizer2D::mouseUp(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (!vp)
        return;

    if (m_curHandle != -1)
    {
        m_handles[m_curHandle].endDrag(vp, ev.x, ev.y);
        m_curHandle = -1;
    }
}

void RadiusSizer2D::render(Viewport* vp, Scene* /*scene*/)
{
    if (!vp)
        return;

    m_overlayHandler.clear();

    for (size_t i = 0; i < m_handles.size(); ++i)
    {
        m_overlayHandler.begin_overlay(static_cast<int32_t>(i));
        m_handles[i].construct(vp, m_overlayHandler);
        m_overlayHandler.set_axis(m_handles[i].axis());
        m_overlayHandler.end_overlay();
    }

    // Vulkan path: Renderer consumes overlayHandler() directly.
}
