#include "CmdDelete.hpp"

#include "Scene.hpp"

bool CmdDelete::execute(Scene* scene)
{
    if (!scene)
        return false;

    const SelectionMode mode = scene->selectionMode();

    // ------------------------------------------------------------
    // 1) Scene-wide: does ANY mesh have a selection?
    // ------------------------------------------------------------
    bool anySelected = false;

    for (SysMesh* mesh : scene->meshes())
    {
        if (!mesh)
            continue;

        switch (mode)
        {
            case SelectionMode::VERTS:
                anySelected = !mesh->selected_verts().empty();
                break;
            case SelectionMode::EDGES:
                anySelected = !mesh->selected_edges().empty();
                break;
            case SelectionMode::POLYS:
                anySelected = !mesh->selected_polys().empty();
                break;
        }

        if (anySelected)
            break;
    }

    // ------------------------------------------------------------
    // 2) Execute delete
    // ------------------------------------------------------------
    bool res = false;

    for (SysMesh* mesh : scene->meshes())
    {
        if (!mesh)
            continue;

        switch (mode)
        {
            case SelectionMode::VERTS: {
                if (anySelected)
                {
                    // Snapshot selection (removals may mutate selection container).
                    const auto sel = mesh->selected_verts();
                    if (sel.empty())
                        break;

                    for (int32_t vid : sel)
                    {
                        if (mesh->vert_valid(vid))
                        {
                            mesh->remove_vert(vid);
                            res = true;
                        }
                    }
                }
                else
                {
                    const auto verts = mesh->all_verts();
                    for (int32_t vid : verts)
                    {
                        if (mesh->vert_valid(vid))
                        {
                            mesh->remove_vert(vid);
                            res = true;
                        }
                    }
                }
                break;
            }

            case SelectionMode::EDGES: {
                if (anySelected)
                {
                    // Snapshot selection.
                    const auto sel = mesh->selected_edges();
                    if (sel.empty())
                        break;

                    for (const IndexPair e : sel)
                    {
                        // Snapshot because poly deletion may mutate adjacency.
                        const auto polys = mesh->edge_polys(e);

                        for (int32_t pid : polys)
                        {
                            if (mesh->poly_valid(pid))
                            {
                                mesh->remove_poly(pid);
                                res = true;
                            }
                        }
                    }
                }
                else
                {
                    const auto edges = mesh->all_edges();
                    for (const IndexPair e : edges)
                    {
                        const auto polys = mesh->edge_polys(e);
                        for (int32_t pid : polys)
                        {
                            if (mesh->poly_valid(pid))
                            {
                                mesh->remove_poly(pid);
                                res = true;
                            }
                        }
                    }
                }
                break;
            }

            case SelectionMode::POLYS: {
                if (anySelected)
                {
                    // Snapshot selection.
                    const auto sel = mesh->selected_polys();
                    if (sel.empty())
                        break;

                    for (int32_t pid : sel)
                    {
                        if (mesh->poly_valid(pid))
                        {
                            mesh->remove_poly(pid);
                            res = true;
                        }
                    }
                }
                else
                {
                    const auto polys = mesh->all_polys();
                    for (int32_t pid : polys)
                    {
                        if (mesh->poly_valid(pid))
                        {
                            mesh->remove_poly(pid);
                            res = true;
                        }
                    }
                }
                break;
            }
        }
    }

    return res;
}
