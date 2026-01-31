#include "BevelTool.hpp"

#include <SysMesh.hpp>

#include "CoreUtilities.hpp"
#include "HeMeshBridge.hpp"
#include "Ops/Bevel.hpp"
#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

BevelTool::BevelTool()
{
    addProperty("Amount", PropertyType::FLOAT, &m_amount, 0.f, 10000.f, 0.05f);
    addProperty("Group edges", PropertyType::BOOL, &m_group);

    m_gizmo.setFollowAmountBase(false);
}

void BevelTool::activate(Scene* /*scene*/)
{
}

void BevelTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_amount))
        return;

    const SelectionMode mode = scene->selectionMode();

    for (SysMesh* mesh : scene->activeMeshes())
    {
        if (!mesh)
            continue;

        if (mode == SelectionMode::VERTS)
        {
            auto sel = mesh->selected_verts();
            if (!sel.empty())
                ops::sys::bevelVerts(mesh, sel, m_amount);

            continue;
        }

        if (mode == SelectionMode::POLYS)
        {
            auto sel = mesh->selected_polys();
            if (!sel.empty())
                ops::sys::bevelPolys(mesh, sel, m_amount, m_group);

            continue;
        }

        if (mode == SelectionMode::EDGES)
        {
            auto sel = mesh->selected_edges();
            if (!sel.empty())
                ops::he::bevelEdges(mesh, sel, m_amount);

            continue;
        }
    }
}

void BevelTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    // Reset preview delta at interaction start.
    m_amount = 0.0f;

    m_gizmo.mouseDown(vp, scene, event);
    propertiesChanged(scene);
}

void BevelTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_gizmo.mouseDrag(vp, scene, event);
    propertiesChanged(scene);
}

void BevelTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_gizmo.mouseUp(vp, scene, event);

    scene->commitMeshChanges();

    // Reset for next interaction.
    m_amount = 0.0f;
}

void BevelTool::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    m_gizmo.render(vp, scene);
}

OverlayHandler* BevelTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
