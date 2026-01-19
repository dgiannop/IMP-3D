#include "CmdSelect.hpp"

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

bool CmdSelectAll::execute(Scene* scene)
{
    if (!scene)
        throw std::runtime_error("CmdSelectAll::execute(): scene is null.");

    const SelectionMode mode = scene->selectionMode();

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->visible())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        switch (mode)
        {
            case SelectionMode::VERTS: {
                // Select all verts
                const auto& verts = mesh->all_verts();
                for (int32_t vi : verts)
                {
                    mesh->select_vert(vi, true);
                }

                // Clear other modes
                mesh->clear_selected_edges();
                mesh->clear_selected_polys();
                break;
            }

            case SelectionMode::EDGES: {
                // Clear first
                mesh->clear_selected_verts();
                mesh->clear_selected_edges();
                mesh->clear_selected_polys();

                const std::vector<IndexPair> edges = mesh->all_edges();
                for (const IndexPair& e : edges)
                {
                    mesh->select_edge(e, true);
                }
                break;
            }

            case SelectionMode::POLYS: {
                // Select all polys
                const auto& polys = mesh->all_polys();
                for (int32_t pi : polys)
                {
                    mesh->select_poly(pi, true);
                }

                // Clear other modes
                mesh->clear_selected_verts();
                mesh->clear_selected_edges();
                break;
            }
        }
    }
    return true;
}

bool CmdSelectNone::execute(Scene* scene)
{
    if (!scene)
        throw std::runtime_error("CmdSelectNone::execute(): scene is null.");

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->visible())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        mesh->clear_selected_verts();
        mesh->clear_selected_edges();
        mesh->clear_selected_polys();
        // todo: map selection
    }
    return true;
}

bool CmdEdgeLoop::execute(Scene* scene)
{
    if (!scene)
        throw std::runtime_error("CmdEdgeLoop::execute(): scene is null.");

    bool anyChanged = false;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->visible())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        const auto& selEdges = mesh->selected_edges();
        if (selEdges.empty())
            continue;

        const IndexPair seed = selEdges.front();

        // Compute loop from SysMesh (stable, canonical edges).
        const std::vector<IndexPair> loop = mesh->edge_loop(seed);
        if (loop.empty())
            continue;

        // Replace selection with the computed loop.
        mesh->clear_selected_edges();

        bool changedThisMesh = false;
        for (const IndexPair& e : loop)
        {
            // edge_loop already returns sorted edges, but select_edge sorts anyway.
            changedThisMesh |= mesh->select_edge(e, true);
        }

        anyChanged |= changedThisMesh;
    }

    return anyChanged;
}

bool CmdEdgeRing::execute(Scene* scene)
{
    if (!scene)
        throw std::runtime_error("CmdEdgeRing::execute(): scene is null.");

    bool anyChanged = false;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->visible())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        const auto& selEdges = mesh->selected_edges();
        if (selEdges.empty())
            continue;

        const IndexPair seed = selEdges.front();

        // Compute ring from SysMesh.
        const std::vector<IndexPair> ring = mesh->edge_ring(seed);
        if (ring.empty())
            continue;

        // Replace selection with the computed ring.
        mesh->clear_selected_edges();

        bool changedThisMesh = false;
        for (const IndexPair& e : ring)
        {
            changedThisMesh |= mesh->select_edge(e, true);
        }

        anyChanged |= changedThisMesh;
    }

    return anyChanged;
}

// -----------------------------------------------------------------------------
// TODOs / Follow-ups
// -----------------------------------------------------------------------------
//
// 1) Performance / history batching:
//    CmdSelectAll currently records one undo action per element.
//    Consider adding batch selection APIs on SysMesh to reduce history spam
//    on large meshes.
//
// 2) Edge loop / ring seed policy:
//    Currently uses the first selected edge as the seed. If multiple edges
//    are selected, this is order-dependent. Consider enforcing a single seed
//    or using a deterministic choice.
//
// 3) Cross-mode selection cleanup:
//    Edge loop / ring commands only replace edge selection.
//    Depending on UX decisions, we may want to clear vertex / polygon
//    selection as well for strict mode isolation.
//
