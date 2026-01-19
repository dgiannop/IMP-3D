#include "MoveTool.hpp"

#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

#ifndef use_test_path
MoveTool::MoveTool() : m_amount{0.f}, m_gizmo{&m_amount}
{
    addProperty("X", PropertyType::FLOAT, &m_amount.x);
    addProperty("Y", PropertyType::FLOAT, &m_amount.y);
    addProperty("Z", PropertyType::FLOAT, &m_amount.z);
}

void MoveTool::activate(Scene*)
{
}

void MoveTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (un::is_zero(m_amount))
        return;

    auto vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 pos = mesh->vert_position(vi);
            mesh->move_vert(vi, pos + m_amount);
        }
    }
}

void MoveTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDown(vp, scene, event);
}

void MoveTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDrag(vp, scene, event);
}

void MoveTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseUp(vp, scene, event);

    scene->commitMeshChanges();

    m_amount = glm::vec3{0.0f};
}

void MoveTool::render(Viewport* vp, Scene* scene)
{
    m_gizmo.render(vp, scene);
}

OverlayHandler* MoveTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}

#else
namespace
{
    constexpr glm::vec3 kAxis = glm::vec3{1.0f, 0.0f, 0.0f}; // Debug: X only
} // namespace
// change to {0,1,0} or {0,0,1} to test Y / Z

MoveTool::MoveTool() : m_amount{0.f}
{
    addProperty("X", PropertyType::FLOAT, &m_amount.x);
    addProperty("Y", PropertyType::FLOAT, &m_amount.y);
    addProperty("Z", PropertyType::FLOAT, &m_amount.z);
}

void MoveTool::activate(Scene*)
{
}

void MoveTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (un::is_zero(m_amount))
        return;

    auto vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 pos = mesh->vert_position(vi);
            mesh->move_vert(vi, pos + m_amount);
        }
    }
}

void MoveTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_startAmount = m_amount;

    // Pivot stabilized against current amount so the drag plane doesn't "walk".
    const glm::vec3 curCenter = sel::selection_center_bounds(scene);
    m_pivot                   = curCenter - m_startAmount;

    const glm::vec3 origin = m_pivot + m_startAmount;

    // Build a plane that contains the axis and faces the camera as much as possible:
    // n = a x (v x a)
    const glm::vec3 a = kAxis; // already unit for X/Y/Z

    glm::vec3 v = vp->viewDirection();
    glm::vec3 n = glm::cross(a, glm::cross(v, a));

    // Degenerate fallback when axis is parallel to view direction.
    if (glm::length2(n) < 1e-8f)
        n = glm::cross(a, vp->upDirection());
    if (glm::length2(n) < 1e-8f)
        n = glm::cross(a, vp->rightDirection());

    if (glm::length2(n) < 1e-8f)
        n = glm::vec3{0.0f, 0.0f, 1.0f};

    m_planeN = glm::normalize(n);

    glm::vec3 hit = origin;
    if (!vp->rayPlaneHit(event.x, event.y, origin, m_planeN, hit))
        hit = origin;

    m_startHit = hit;
}

void MoveTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    const glm::vec3 origin = m_pivot + m_startAmount;

    glm::vec3 hit = origin;
    if (!vp->rayPlaneHit(event.x, event.y, origin, m_planeN, hit))
        return;

    const glm::vec3 d = hit - m_startHit;

    // Project motion onto the axis.
    const float t = glm::dot(d, kAxis);

    // Only X changes in this debug mode.
    m_amount = m_startAmount + kAxis * t;
}

void MoveTool::mouseUp(Viewport*, Scene* scene, const CoreEvent&)
{
    if (scene)
        scene->commitMeshChanges();

    m_amount = glm::vec3{0.0f};
}

void MoveTool::render(Viewport* vp, Scene* scene)
{
}

OverlayHandler* MoveTool::overlayHandler()
{
    return nullptr;
    //&m_gizmo.overlayHandler();
}
#endif
