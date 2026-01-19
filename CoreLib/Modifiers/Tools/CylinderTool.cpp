// CylinderTool.cpp
#include "CylinderTool.hpp"

#include <glm/gtc/epsilon.hpp>
#include <limits>

#include "Primitives.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"

CylinderTool::CylinderTool() :
    m_sceneMesh{nullptr},
    m_radius{0.5f},
    m_height{1.0f},
    m_center{0.f},
    m_sides{24},
    m_segments{4},
    m_axis{0, 1, 0},
    m_caps{true},
    m_radiusResizer{&m_radius, &m_height, &m_center, &m_axis}
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

void CylinderTool::activate(Scene*)
{
}

void CylinderTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (!m_sceneMesh)
    {
        m_sceneMesh = scene->createSceneMesh();
    }

    const float eps = std::numeric_limits<float>::epsilon();

    if (glm::epsilonNotEqual(m_radius, 0.0f, eps) && glm::epsilonNotEqual(m_height, 0.0f, eps))
    {
        SysMesh* mesh = m_sceneMesh->sysMesh();

        const int sides = std::max(3, m_sides);
        const int segs  = std::max(1, m_segments);

        Primitives::createCylinder(mesh, m_center, m_axis, m_radius, m_height, sides, segs, m_caps);
    }
}

void CylinderTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_radiusResizer.mouseDown(vp, scene, event);
}

void CylinderTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_radiusResizer.mouseDrag(vp, scene, event);
}

void CylinderTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_radiusResizer.mouseUp(vp, scene, event);
}

void CylinderTool::render(Viewport* vp, Scene* scene)
{
    m_radiusResizer.render(vp, scene);
}

OverlayHandler* CylinderTool::overlayHandler()
{
    return &m_radiusResizer.overlayHandler();
}
