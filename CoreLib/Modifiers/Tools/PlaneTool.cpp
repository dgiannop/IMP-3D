//=============================================================================
// PlaneTool.cpp
//=============================================================================
#include "PlaneTool.hpp"

#include <algorithm>

#include "Primitives.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

PlaneTool::PlaneTool() :
    m_sceneMesh{nullptr},
    m_size{1.f, 1.f},
    m_center{0.f},
    m_segs{1, 1},
    m_axis{0, 1, 0},
    m_gizmo{&m_center, &m_size, &m_axis}
{
    addProperty("Width", PropertyType::FLOAT, &m_size.x, 0.f);
    addProperty("Height", PropertyType::FLOAT, &m_size.y, 0.f);

    addProperty("Center X", PropertyType::FLOAT, &m_center.x);
    addProperty("Center Y", PropertyType::FLOAT, &m_center.y);
    addProperty("Center Z", PropertyType::FLOAT, &m_center.z);

    addProperty("Segments U", PropertyType::INT, &m_segs.x, 1, 128);
    addProperty("Segments V", PropertyType::INT, &m_segs.y, 1, 128);

    addProperty("Axis", PropertyType::AXIS, &m_axis);
}

void PlaneTool::activate(Scene*)
{
}

void PlaneTool::propertiesChanged(Scene* scene)
{
    scene->abortMeshChanges();

    if (!m_sceneMesh)
        m_sceneMesh = scene->createSceneMesh();

    if (m_size.x > 0.0f && m_size.y > 0.0f)
    {
        SysMesh* mesh = m_sceneMesh->sysMesh();

        const glm::ivec2 segs{
            std::max(1, m_segs.x),
            std::max(1, m_segs.y),
        };

        Primitives::createPlane(mesh, m_center, m_axis, m_size, segs);
    }
}

void PlaneTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDown(vp, scene, event);
}

void PlaneTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDrag(vp, scene, event);
}

void PlaneTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseUp(vp, scene, event);
}

void PlaneTool::render(Viewport* vp, Scene* scene)
{
    m_gizmo.render(vp, scene);
}

OverlayHandler* PlaneTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
