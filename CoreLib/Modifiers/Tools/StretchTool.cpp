#include "StretchTool.hpp"

#include <algorithm>
#include <cmath>

#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    static glm::vec3 clampScale(const glm::vec3& s)
    {
        glm::vec3 out = s;
        out.x         = std::max(0.0001f, out.x);
        out.y         = std::max(0.0001f, out.y);
        out.z         = std::max(0.0001f, out.z);
        return out;
    }
} // namespace

StretchTool::StretchTool()
{
    addProperty("X", PropertyType::FLOAT, &m_scale.x);
    addProperty("Y", PropertyType::FLOAT, &m_scale.y);
    addProperty("Z", PropertyType::FLOAT, &m_scale.z);

    m_scale = glm::vec3{1.0f, 1.0f, 1.0f};
}

void StretchTool::activate(Scene*)
{
}

void StretchTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_scale - glm::vec3{1.0f}))
        return;

    const glm::vec3 s = clampScale(m_scale);

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

void StretchTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    // Reset preview at interaction start.
    m_scale = glm::vec3{1.0f, 1.0f, 1.0f};

    m_gizmo.mouseDown(vp, scene, event);
    propertiesChanged(scene);
}

void StretchTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_gizmo.mouseDrag(vp, scene, event);

    m_scale = clampScale(m_scale);

    propertiesChanged(scene);
}

void StretchTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_gizmo.mouseUp(vp, scene, event);

    scene->commitMeshChanges();

    // Reset for next interaction.
    m_scale = glm::vec3{1.0f, 1.0f, 1.0f};
}

void StretchTool::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    m_gizmo.render(vp, scene);
}

OverlayHandler* StretchTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
