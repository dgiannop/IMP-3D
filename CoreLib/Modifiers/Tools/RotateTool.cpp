#include "RotateTool.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    static glm::quat eulerDegToQuatXYZ(const glm::vec3& deg)
    {
        const glm::vec3 rad = glm::radians(deg);

        // Apply in X then Y then Z (Pitch, Yaw, Roll)
        const glm::quat qx = glm::angleAxis(rad.x, glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::quat qy = glm::angleAxis(rad.y, glm::vec3{0.0f, 1.0f, 0.0f});
        const glm::quat qz = glm::angleAxis(rad.z, glm::vec3{0.0f, 0.0f, 1.0f});

        return glm::normalize(qz * qy * qx);
    }
} // namespace

RotateTool::RotateTool()
{
    addProperty("Pitch", PropertyType::FLOAT, &m_anglesDeg.x);
    addProperty("Yaw", PropertyType::FLOAT, &m_anglesDeg.y);
    addProperty("Roll", PropertyType::FLOAT, &m_anglesDeg.z);

    // Rotate is a delta tool param; keep it zero by default.
    m_anglesDeg = glm::vec3{0.0f};
}

void RotateTool::activate(Scene*)
{
    // Nothing yet.
}

void RotateTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    // Interactive tool pattern: revert previous preview, then re-apply current delta.
    scene->abortMeshChanges();

    if (un::is_zero(m_anglesDeg))
        return;

    const glm::quat q     = eulerDegToQuatXYZ(m_anglesDeg);
    const glm::vec3 pivot = sel::selection_center_bounds(scene);

    auto vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 p  = mesh->vert_position(vi);
            const glm::vec3 r  = p - pivot;
            const glm::vec3 rp = q * r;
            mesh->move_vert(vi, pivot + rp);
        }
    }
}

void RotateTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    // This tool uses angles as a *delta* from the current scene state.
    // Start each interaction at zero so the preview is stable.
    m_anglesDeg = glm::vec3{0.0f};

    m_gizmo.mouseDown(vp, scene, event);

    // Apply immediately in case the gizmo sets an initial value (usually it won't).
    propertiesChanged(scene);
}

void RotateTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_gizmo.mouseDrag(vp, scene, event);

    // Rebuild preview for the new angle delta.
    propertiesChanged(scene);
}

void RotateTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_gizmo.mouseUp(vp, scene, event);

    // Freeze the preview into real geometry.
    scene->commitMeshChanges();

    // Reset delta so subsequent drags start clean.
    m_anglesDeg = glm::vec3{0.0f};
}

void RotateTool::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    m_gizmo.render(vp, scene);
}

OverlayHandler* RotateTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
