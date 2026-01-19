#include "BoxSizer.hpp"

#include "Viewport.hpp"

// Helper: Setup min/max from size and center
static void set_min_max_from_center_size(const glm::vec3& center, const glm::vec3& size, glm::vec3& min, glm::vec3& max)
{
    min = center - size * 0.5f;
    max = center + size * 0.5f;
}

// Helper: Update size and center from min/max
static void set_center_size_from_min_max(const glm::vec3& min, const glm::vec3& max, glm::vec3& center, glm::vec3& size)
{
    center = (min + max) * 0.5f;
    size   = max - min;
}

BoxSizer::BoxSizer(glm::vec3* size, glm::vec3* center) : m_size(size), m_center(center)
{
    // Initialize min/max from provided center/size
    set_min_max_from_center_size(*m_center, *m_size, m_min, m_max);

    // Define all handles with min/max pointers
    m_handles.emplace_back(glm::ivec3{+1, +1, +1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{-1, +1, +1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{-1, -1, +1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{-1, -1, -1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{+1, -1, -1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{+1, +1, -1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{+1, -1, +1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{-1, +1, -1}, &m_min, &m_max);

    // Center and face/edge handles
    m_handles.emplace_back(glm::ivec3{0, 0, 0}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{0, +1, 0}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{0, -1, 0}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{+1, 0, 0}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{-1, 0, 0}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{0, 0, +1}, &m_min, &m_max);
    m_handles.emplace_back(glm::ivec3{0, 0, -1}, &m_min, &m_max);
}

void BoxSizer::mouseDown(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    // Pick handle
    std::string name = m_overlayHandler.pick(vp, ev.x, ev.y);
    m_curHandle      = (name != "") ? std::atoi(name.c_str()) : -1;

    if (m_curHandle != -1)
        m_handles[m_curHandle].beginDrag(vp, ev.x, ev.y);
}

void BoxSizer::mouseDrag(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (m_curHandle != -1)
    {
        m_handles[m_curHandle].drag(vp, ev.x, ev.y);
        set_center_size_from_min_max(m_min, m_max, *m_center, *m_size);
    }
}

void BoxSizer::mouseUp(Viewport* vp, Scene* /*scene*/, const CoreEvent& ev)
{
    if (m_curHandle != -1)
    {
        m_handles[m_curHandle].endDrag(vp, ev.x, ev.y);
        m_curHandle = -1;
    }
}

void BoxSizer::render(Viewport* vp, Scene* /*scene*/)
{
    // make sure min/max follow current size/center
    set_min_max_from_center_size(*m_center, *m_size, m_min, m_max);

    m_overlayHandler.clear();

    for (size_t i = 0; i < m_handles.size(); ++i)
    {
        m_overlayHandler.begin_overlay(std::to_string(i));
        m_handles[i].construct(vp, m_overlayHandler);
        m_overlayHandler.set_axis(m_handles[i].axis());
        m_overlayHandler.end_overlay();
    }

    // m_shapeHandler.render(vp);
    // In Vulkan path, render() is a no-op; Renderer uses overlayHandler() directly.
    // m_overlayHandler.render(vp);
}
