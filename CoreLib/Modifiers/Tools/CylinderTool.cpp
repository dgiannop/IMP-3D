#include "CylinderTool.hpp"

#include "CylinderGizmo.hpp"
#include "Primitives.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"

CylinderTool::CylinderTool() :
    m_sceneMesh{nullptr},
    m_radius{0.5f},
    m_height{1.0f},
    m_center{glm::vec3{0.f}},
    m_sides{24},
    m_segments{4},
    m_axis{glm::ivec3{0, 1, 0}},
    m_caps{true},
    m_gizmo{&m_center, &m_radius, &m_height}
{
    addProperty("Radius", PropertyType::FLOAT, &m_radius, 0.f);
    addProperty("Height", PropertyType::FLOAT, &m_height, 0.f);

    addProperty("Center X", PropertyType::FLOAT, &m_center.x);
    addProperty("Center Y", PropertyType::FLOAT, &m_center.y);
    addProperty("Center Z", PropertyType::FLOAT, &m_center.z);

    addProperty("Sides", PropertyType::INT, &m_sides, 3, 128);
    addProperty("Segments", PropertyType::INT, &m_segments, 1, 128);

    addProperty("Axis", PropertyType::AXIS, &m_axis);
    addProperty("Caps", PropertyType::BOOL, &m_caps);
}

void CylinderTool::activate(Scene* scene)
{
}

void CylinderTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (!m_sceneMesh)
        m_sceneMesh = scene->createSceneMesh();

    if (m_radius > 0.0f && m_height > 0.0f)
    {
        SysMesh* mesh = m_sceneMesh->sysMesh();

        const int sides = std::max(3, m_sides);
        const int segs  = std::max(1, m_segments);

        Primitives::createCylinder(mesh, m_center, m_axis, m_radius, m_height, sides, segs, m_caps);
    }
}

void CylinderTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDown(vp, scene, event);
}

void CylinderTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDrag(vp, scene, event);
}

void CylinderTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseUp(vp, scene, event);
}

void CylinderTool::render(Viewport* vp, Scene* scene)
{
    m_gizmo.render(vp, scene);
}

OverlayHandler* CylinderTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
