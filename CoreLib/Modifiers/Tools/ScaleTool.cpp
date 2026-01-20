#include "ScaleTool.hpp"

#include <algorithm>

#include "Scene.hpp"
#include "Viewport.hpp"

ScaleTool::ScaleTool()
{
    addProperty("Scale", PropertyType::FLOAT, &m_scale);
}

void ScaleTool::activate(Scene*)
{
}

void ScaleTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_scale - 1.0f))
        return;

    // Prevent weird flips / division by zero scale.
    const float s = std::max(0.0001f, m_scale);

    const glm::vec3 pivot   = sel::selection_center_bounds(scene);
    auto            vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 p = mesh->vert_position(vi);
            mesh->move_vert(vi, pivot + (p - pivot) * s);
        }
    }
}

void ScaleTool::mouseDown(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_startScale = m_scale;
    m_pivot      = sel::selection_center_bounds(scene);
    m_startX     = event.x;
}

void ScaleTool::mouseDrag(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    const int32_t dx = event.x - m_startX;

    // Exponential-ish feel: scale grows smoothly with pixels.
    // 100px -> ~2x, -100px -> ~0.5x
    constexpr float k      = 1.0f / 100.0f;
    const float     factor = std::pow(2.0f, float(dx) * k);

    m_scale = m_startScale * factor;
    if (m_scale < 0.0001f)
        m_scale = 0.0001f;
}

void ScaleTool::mouseUp(Viewport*, Scene* scene, const CoreEvent&)
{
    if (scene)
        scene->commitMeshChanges();

    m_scale = 1.0f;
}

void ScaleTool::render(Viewport*, Scene*)
{
}

OverlayHandler* ScaleTool::overlayHandler()
{
    return nullptr;
}
