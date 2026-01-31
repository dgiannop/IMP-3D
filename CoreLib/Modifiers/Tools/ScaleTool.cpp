#include "ScaleTool.hpp"

#include <algorithm>
#include <cmath>

#include "Scene.hpp"
#include "Viewport.hpp"

ScaleTool::ScaleTool()
{
    addProperty("Scale", PropertyType::FLOAT, &m_uniformScale);

    m_uniformScale = 1.0f;
    m_scale        = glm::vec3{1.0f};
}

void ScaleTool::activate(Scene*)
{
}

void ScaleTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    // Property-panel scale is UNIFORM only.
    // Keep it clamped and mirror into the gizmo scale vector.
    const float sUI = std::max(0.0001f, m_uniformScale);
    if (un::is_zero(sUI - 1.0f))
        return;

    m_scale = glm::vec3{sUI};

    const glm::vec3 pivot   = sel::selection_center_bounds(scene);
    auto            vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 p = mesh->vert_position(vi);
            mesh->move_vert(vi, pivot + (p - pivot) * m_scale);
        }
    }
}

void ScaleTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    // Reset preview delta at interaction start.
    m_scale        = glm::vec3{1.0f};
    m_uniformScale = 1.0f;

    m_gizmo.mouseDown(vp, scene, event);

    // Apply preview immediately (even if the first down didn't hit a handle,
    // this will no-op because m_scale is still 1).
    scene->abortMeshChanges();

    // Use current m_scale from gizmo if it started dragging.
    const glm::vec3 s = glm::max(m_scale, glm::vec3{0.0001f});
    if (un::is_zero(s.x - 1.0f) && un::is_zero(s.y - 1.0f) && un::is_zero(s.z - 1.0f))
        return;

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

void ScaleTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_gizmo.mouseDrag(vp, scene, event);

    // Clamp: negative/zero scale not supported in this tool.
    m_scale = glm::max(m_scale, glm::vec3{0.0001f});

    // Only update the UI scalar when the scale is effectively uniform.
    // This avoids the property panel jumping around during axis-only scale.
    const float eps = 1e-4f;
    if (std::abs(m_scale.x - m_scale.y) < eps && std::abs(m_scale.x - m_scale.z) < eps)
        m_uniformScale = m_scale.x;

    scene->abortMeshChanges();

    const glm::vec3 s = m_scale;
    if (un::is_zero(s.x - 1.0f) && un::is_zero(s.y - 1.0f) && un::is_zero(s.z - 1.0f))
        return;

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

void ScaleTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_gizmo.mouseUp(vp, scene, event);

    scene->commitMeshChanges();

    // Reset deltas for the next interaction.
    m_scale        = glm::vec3{1.0f};
    m_uniformScale = 1.0f;
}

void ScaleTool::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    m_gizmo.render(vp, scene);
}

OverlayHandler* ScaleTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
