#include "SphereTool.hpp"

#include <glm/gtc/epsilon.hpp>

#include "Primitives.hpp"
#include "Scene.hpp"

SphereTool::SphereTool() :
    m_sceneMesh{nullptr},
    m_radius{0.5f},
    m_center{0.f},
    m_axis{0, 1, 0},
    m_sides{22},
    m_rings{16},
    m_radiusResizer{&m_radius, &m_center}
{
    addProperty("Radius X", PropertyType::FLOAT, &m_radius.x);
    addProperty("Radius Y", PropertyType::FLOAT, &m_radius.y);
    addProperty("Radius Z", PropertyType::FLOAT, &m_radius.z);
    addProperty("Center X", PropertyType::FLOAT, &m_center.x);
    addProperty("Center Y", PropertyType::FLOAT, &m_center.y);
    addProperty("Center Z", PropertyType::FLOAT, &m_center.z);
    addProperty("Sides", PropertyType::INT, &m_sides, 3, 32);
    addProperty("Rings", PropertyType::INT, &m_rings, 2, 32);
    addProperty("Axis", PropertyType::AXIS, &m_axis);
    addProperty("Smooth Normals", PropertyType::BOOL, &m_smooth);
}

void SphereTool::activate(Scene*)
{
}

void SphereTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (!m_sceneMesh)
    {
        m_sceneMesh = scene->createSceneMesh();
    }

    if (glm::all(glm::epsilonNotEqual(m_radius, glm::vec3(0.f), std::numeric_limits<float>::epsilon())))
    {
        SysMesh* mesh = m_sceneMesh->sysMesh();

        Primitives::createSphere(mesh, m_center, m_axis, m_radius, m_rings, m_sides, m_smooth);
    }
}

void SphereTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_radiusResizer.mouseDown(vp, scene, event);
}

void SphereTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_radiusResizer.mouseDrag(vp, scene, event);
}

void SphereTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_radiusResizer.mouseUp(vp, scene, event);
}

void SphereTool::render(Viewport* vp, Scene* scene)
{
    m_radiusResizer.render(vp, scene);
}

OverlayHandler* SphereTool::overlayHandler()
{
    return &m_radiusResizer.overlayHandler();
}
