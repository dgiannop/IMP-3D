#include "BevelTool.hpp"

#include <SysMesh.hpp>
#include <vector>

#include "BevelTool.hpp"
#include "CoreUtilities.hpp"
#include "HeMeshBridge.hpp"
#include "Ops/Bevel.hpp"
#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "SysMesh.hpp"
#include "Viewport.hpp"

BevelTool::BevelTool()
{
    addProperty("Amount", PropertyType::FLOAT, &m_amount, 0.f, 10000.f, 0.05f);
    addProperty("Group edges", PropertyType::BOOL, &m_group);
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

void BevelTool::mouseDown(Viewport* /*vp*/, Scene* /*scene*/, const CoreEvent& /*event*/)
{
}

void BevelTool::mouseDrag(Viewport* vp, Scene* /*scene*/, const CoreEvent& event)
{
    m_amount += event.deltaX * vp->pixelScale();
    m_amount += event.deltaY * vp->pixelScale();
}

void BevelTool::mouseUp(Viewport* /*vp*/, Scene* /*scene*/, const CoreEvent& /*event*/)
{
}
