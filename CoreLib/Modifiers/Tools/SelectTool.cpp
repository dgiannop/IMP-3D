#include "SelectTool.hpp"

#include "Viewport.hpp"

namespace
{
    static bool apply_edge_loop(SysMesh* mesh, const IndexPair& seedSorted, bool addMode)
    {
        if (!mesh)
            return false;

        const std::vector<IndexPair> loop = mesh->edge_loop(seedSorted);
        if (loop.empty())
            return false;

        if (!addMode)
            mesh->clear_selected_edges();

        bool changed = false;
        for (const IndexPair& e : loop)
            changed |= mesh->select_edge(e, true);

        return changed;
    }

    static bool apply_poly_loop(SysMesh* mesh, int32_t anchorPoly, int32_t dirPoly, bool addMode)
    {
        if (!mesh)
            return false;

        if (!mesh->poly_valid(anchorPoly) || !mesh->poly_valid(dirPoly))
            return false;

        // Find shared edge between the two polys
        IndexPair seed{-1, -1};
        for (IndexPair e : mesh->poly_edges(anchorPoly))
        {
            if (mesh->poly_has_edge(dirPoly, e))
            {
                seed = SysMesh::sort_edge(e);
                break;
            }
        }

        if (seed.first < 0)
            return false; // not adjacent

        const std::vector<IndexPair> ringEdges = mesh->edge_ring(seed);
        if (ringEdges.empty())
            return false;

        if (!addMode)
            mesh->clear_selected_polys();

        bool any = false;

        // Select all polys incident to ring edges (strip/band)
        for (const IndexPair& e : ringEdges)
        {
            for (int32_t pid : mesh->edge_polys(e))
            {
                if (mesh->poly_valid(pid))
                    any |= mesh->select_poly(pid, true);
            }
        }

        return any;
    }

    static bool apply_vert_loop(SysMesh* mesh, int32_t v0, int32_t v1, bool addMode)
    {
        if (!mesh)
            return false;

        if (!mesh->vert_valid(v0) || !mesh->vert_valid(v1))
            return false;

        const IndexPair              seed      = SysMesh::sort_edge(IndexPair{v0, v1});
        const std::vector<IndexPair> loopEdges = mesh->edge_loop(seed);
        if (loopEdges.empty())
            return false;

        if (!addMode)
            mesh->clear_selected_verts();

        bool any = false;
        for (const IndexPair& e : loopEdges)
        {
            any |= mesh->select_vert(e.first, true);
            any |= mesh->select_vert(e.second, true);
        }

        return any;
    }

} // namespace

SelectTool::SelectTool()
{
    addProperty("Select Through", PropertyType::BOOL, &m_selectThrough);
}

void SelectTool::activate(Scene*)
{
}

void SelectTool::propertiesChanged(Scene* scene)
{
}

void SelectTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    const un::ray ray = vp->ray(event.x, event.y);

    // ------------------------------------------------------------
    // Query hit appropriate for the current selection mode
    // ------------------------------------------------------------
    MeshHit hit;

    switch (scene->selectionMode())
    {
        case SelectionMode::VERTS:
            hit = scene->sceneQuery()->queryVert(vp, scene, ray);
            break;

        case SelectionMode::EDGES:
            hit = scene->sceneQuery()->queryEdge(vp, scene, ray);
            break;

        case SelectionMode::POLYS:
            hit = scene->sceneQuery()->queryPoly(vp, scene, ray);
            break;
    }

    // ------------------------------------------------------------
    // If no hit, clear selection (unless shift) and bail
    // ------------------------------------------------------------
    if (!hit.valid())
    {
        if (!event.shift_key)
        {
            scene->clearSelection();
            m_addMode = true;
        }
        return;
    }

    // ------------------------------------------------------------
    // Determine add mode (shift OR no existing selection)
    // ------------------------------------------------------------
    bool hasSelection = false;

    switch (scene->selectionMode())
    {
        case SelectionMode::VERTS:
            for (const SysMesh* mesh : scene->activeMeshes())
            {
                if (mesh && !mesh->selected_verts().empty())
                {
                    hasSelection = true;
                    break;
                }
            }
            break;

        case SelectionMode::EDGES:
            for (const SysMesh* mesh : scene->activeMeshes())
            {
                if (mesh && !mesh->selected_edges().empty())
                {
                    hasSelection = true;
                    break;
                }
            }
            break;

        case SelectionMode::POLYS:
            for (const SysMesh* mesh : scene->activeMeshes())
            {
                if (mesh && !mesh->selected_polys().empty())
                {
                    hasSelection = true;
                    break;
                }
            }
            break;
    }

    m_addMode = event.shift_key || !hasSelection;

    // ------------------------------------------------------------
    // Special gestures: loops
    // ------------------------------------------------------------

    // Edge Loop (dbl-click OR alt-click)
    if (scene->selectionMode() == SelectionMode::EDGES && (event.dbl_click || event.alt_key))
    {
        SysMesh* mesh = hit.mesh ? hit.mesh->sysMesh() : nullptr;
        if (mesh)
        {
            const IndexPair seed = SysMesh::sort_edge(IndexPair{hit.index, hit.other});
            apply_edge_loop(mesh, seed, true);
        }
        return;
    }

    // Poly Loop (shift + dbl-click)
    if (scene->selectionMode() == SelectionMode::POLYS && event.shift_key && event.dbl_click)
    {
        SysMesh* mesh = hit.mesh ? hit.mesh->sysMesh() : nullptr;
        if (!mesh)
            return;

        const auto& sel = mesh->selected_polys();
        if (sel.size() >= 2)
        {
            const int32_t prev = sel[sel.size() - 2];
            const int32_t next = sel.back();
            apply_poly_loop(mesh, prev, next, true);
        }
        return;
    }

    // Vert Loop (shift + dbl-click)
    if (scene->selectionMode() == SelectionMode::VERTS && event.shift_key && event.dbl_click)
    {
        SysMesh* mesh = hit.mesh ? hit.mesh->sysMesh() : nullptr;
        if (!mesh)
            return;

        const auto& sel = mesh->selected_verts();
        if (sel.size() >= 2)
        {
            const int32_t prev = sel[sel.size() - 2];
            const int32_t next = sel.back();
            apply_vert_loop(mesh, prev, next, true);
        }
        return;
    }

    // ------------------------------------------------------------
    // Normal selection
    // ------------------------------------------------------------
    mouseDrag(vp, scene, event);
}

void SelectTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    const un::ray ray = vp->ray(event.x, event.y);

    if (!m_selectThrough)
    {
        MeshHit hit;

        switch (scene->selectionMode())
        {
            case SelectionMode::VERTS:
                hit = scene->sceneQuery()->queryVert(vp, scene, ray);
                if (hit.valid())
                    hit.mesh->sysMesh()->select_vert(hit.index, m_addMode);
                break;

            case SelectionMode::EDGES:
                hit = scene->sceneQuery()->queryEdge(vp, scene, ray);
                if (hit.valid() && (hit.other > -1))
                    hit.mesh->sysMesh()->select_edge({hit.index, hit.other}, m_addMode);
                break;

            case SelectionMode::POLYS: {
                hit = scene->sceneQuery()->queryPoly(vp, scene, ray);
                if (hit.valid())
                    hit.mesh->sysMesh()->select_poly(hit.index, m_addMode);
                break;
            }
        }
    }
    else
    {
        switch (scene->selectionMode())
        {
            case SelectionMode::VERTS: {
                auto hitList = scene->sceneQuery()->queryVerts(vp, scene, ray);
                for (auto hit : hitList)
                {
                    if (hit.valid())
                        hit.mesh->sysMesh()->select_vert(hit.index, m_addMode);
                }
                break;
            }
            case SelectionMode::EDGES: {
                auto hitList = scene->sceneQuery()->queryEdges(vp, scene, ray);
                for (auto hit : hitList)
                {
                    if (hit.valid() && (hit.other > -1))
                        hit.mesh->sysMesh()->select_edge({hit.index, hit.other}, m_addMode);
                }
                break;
            }
            case SelectionMode::POLYS: {
                auto hitList = scene->sceneQuery()->queryPolys(vp, scene, ray);
                for (auto hit : hitList)
                {
                    if (hit.valid())
                        hit.mesh->sysMesh()->select_poly(hit.index, m_addMode);
                }
                break;
            }
        }
    }
}

void SelectTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    scene->commitMeshChanges();
}

void SelectTool::render(Viewport* vp, Scene* scene)
{
}
