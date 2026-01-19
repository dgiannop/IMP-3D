#include "CmdDelete.hpp"

#include "Scene.hpp"

bool CmdDelete::execute(Scene* scene)
{
    bool res = false;

    for (SysMesh* mesh : scene->meshes())
    {
        switch (scene->selectionMode())
        {
            case SelectionMode::VERTS: {
                auto verts = mesh->selected_verts().empty() ? mesh->all_verts() : mesh->selected_verts();
                for (int32_t vid : verts)
                {
                    if (mesh->vert_valid(vid))
                    {
                        mesh->remove_vert(vid);
                        res = true;
                    }
                }
                break;
            }
            case SelectionMode::EDGES: {
                auto edges = mesh->selected_edges().empty() ? mesh->all_edges() : mesh->selected_edges();
                for (IndexPair pair : edges)
                {
                    for (int32_t pid : mesh->edge_polys(pair))
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
            case SelectionMode::POLYS: {
                auto polys = mesh->selected_polys().empty() ? mesh->all_polys() : mesh->selected_polys();
                for (int32_t pid : polys)
                {
                    if (mesh->poly_valid(pid))
                    {
                        mesh->remove_poly(pid);
                        res = true;
                    }
                }
                break;
            }
        }
    }

    return res;
}
