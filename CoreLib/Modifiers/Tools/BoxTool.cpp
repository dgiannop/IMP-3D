#include "BoxTool.hpp"

// #include <glm/gtc/epsilon.hpp>

#include "CoreUtilities.hpp"
#include "Primitives.hpp"
#include "SysMesh.hpp"

BoxTool::BoxTool() : m_sceneMesh{nullptr},
                     m_size{1.f},
                     m_center{0.f},
                     m_segs{3}

{
    addProperty("Width", PropertyType::FLOAT, &m_size.x);
    addProperty("Height", PropertyType::FLOAT, &m_size.y);
    addProperty("Depth", PropertyType::FLOAT, &m_size.z);
    addProperty("Center X", PropertyType::FLOAT, &m_center.x);
    addProperty("Center Y", PropertyType::FLOAT, &m_center.y);
    addProperty("Center Z", PropertyType::FLOAT, &m_center.z);
    addProperty("Segments X", PropertyType::INT, &m_segs.x, 1, 64);
    addProperty("Segments Y", PropertyType::INT, &m_segs.y, 1, 64);
    addProperty("Segments Z", PropertyType::INT, &m_segs.z, 1, 64);
}

void BoxTool::activate(Scene*)
{
}

void BoxTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (!m_sceneMesh)
    {
        m_sceneMesh = scene->createSceneMesh();
    }

    if (!un::is_zero(m_size))
    {
        SysMesh* mesh = m_sceneMesh->sysMesh();

        Primitives::createBox(mesh, m_center, m_size, m_segs);
    }
}

void BoxTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDown(vp, scene, event);
}

void BoxTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDrag(vp, scene, event);
}

void BoxTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseUp(vp, scene, event);
}

void BoxTool::render(Viewport* vp, Scene* scene)
{
    m_gizmo.render(vp, scene);
}

OverlayHandler* BoxTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
