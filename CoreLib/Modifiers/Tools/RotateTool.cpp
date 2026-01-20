#include "RotateTool.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
}

void RotateTool::activate(Scene*)
{
}

void RotateTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

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

void RotateTool::mouseDown(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_startAnglesDeg = m_anglesDeg;
    m_pivot          = sel::selection_center_bounds(scene);
    m_startX         = event.x;
    m_startY         = event.y;
}

void RotateTool::mouseDrag(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    const int32_t dx = event.x - m_startX;
    const int32_t dy = event.y - m_startY;

    // Very basic mapping: drag X -> yaw, drag Y -> pitch (inverted Y typical)
    constexpr float kDegPerPixel = 0.25f;

    m_anglesDeg = m_startAnglesDeg;
    m_anglesDeg.y += float(dx) * kDegPerPixel;
    m_anglesDeg.x += float(-dy) * kDegPerPixel;
}

void RotateTool::mouseUp(Viewport*, Scene* scene, const CoreEvent&)
{
    if (scene)
        scene->commitMeshChanges();

    m_anglesDeg = glm::vec3{0.0f};
}

void RotateTool::render(Viewport*, Scene*)
{
}

OverlayHandler* RotateTool::overlayHandler()
{
    return nullptr;
}
