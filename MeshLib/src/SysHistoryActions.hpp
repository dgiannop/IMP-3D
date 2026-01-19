#ifndef SYS_HISTORY_ACTIONS_HPP_INCLUDED
#define SYS_HISTORY_ACTIONS_HPP_INCLUDED

#include "SysMesh.hpp"
#include "SysMeshData.hpp"

struct UndoCreateVertex : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->remove_vert(vert_index);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        [[maybe_unused]] int32_t new_index = mesh->create_vert(vert_pos);
        assert(new_index == vert_index);
    }

    glm::vec3 vert_pos;
    int       vert_index;
};

// -------------------------------------------------------------------------------

struct UndoMoveVertex : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh*        mesh    = static_cast<SysMesh*>(data);
        const glm::vec3 new_pos = mesh->vert_position(vert_index);
        mesh->move_vert(vert_index, old_pos);
        old_pos = new_pos;
    }

    virtual void redo(void* data) override
    {
        undo(data);
    }

    glm::vec3 old_pos;
    int       vert_index;
};

// -------------------------------------------------------------------------------

struct UndoRemoveVertex : public HistoryAction
{
    void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        assert(mesh && "UndoRemoveVertex::undo: mesh is null");
        assert(mesh_data && "UndoRemoveVertex::undo: mesh_data is null");
        assert(vert_index >= 0 && "UndoRemoveVertex::undo: invalid vert_index");

        // Recreate vertex and assert stable index (HoleList LIFO invariant).
        [[maybe_unused]] const int32_t new_index = mesh->create_vert(vert_pos);
        assert(new_index == vert_index && "UndoRemoveVertex::undo: vertex index drifted (freelist order broken?)");
        assert(mesh->vert_valid(vert_index) && "UndoRemoveVertex::undo: restored vertex is not valid");

        // Restore base polygons + vertex adjacency.
        for (SysFullPoly& p : polys)
        {
            assert(p.index >= 0 && "UndoRemoveVertex::undo: invalid poly index");

            // Restore the poly slot (even if previously removed).
            if (!mesh->poly_valid(p.index))
            {
                // Force slot restore (you rely on capacity being large enough).
                // This assumes HoleList capacity wasn't shrunk; should hold in your design.
                mesh_data->polys[p.index] = p.data;
            }
            else
            {
                mesh_data->polys[p.index].verts       = p.data.verts;
                mesh_data->polys[p.index].removed     = false;
                mesh_data->polys[p.index].material_id = p.data.material_id;
                mesh_data->polys[p.index].selected    = p.data.selected;
            }

            // Restore vert -> polys adjacency for all verts referenced by this poly.
            for (int32_t vi : p.data.verts)
            {
                assert(vi >= 0 && "UndoRemoveVertex::undo: poly contains negative vert index");
                // If you ever allow polys to reference removed verts, this will trip.
                assert(mesh->vert_valid(vi) && "UndoRemoveVertex::undo: poly references invalid vert");

                mesh_data->verts[vi].polys.insert_unique(p.index);
            }
        }

        // Restore map polys (only if map still exists).
        for (SysFullMapPoly& mp : map_polys)
        {
            assert(mp.map >= 0 && "UndoRemoveVertex::undo: invalid map index");
            assert(mp.index >= 0 && "UndoRemoveVertex::undo: invalid map poly index");
            assert(mp.map < static_cast<int32_t>(mesh_data->mesh_maps.size()) &&
                   "UndoRemoveVertex::undo: map index out of range");

            auto& mapPtr = mesh_data->mesh_maps[mp.map];
            if (!mapPtr)
            {
                // Map was removed after this action was recorded.
                // Robust behavior: skip restore instead of crashing.
                continue;
            }

            // Ensure map polys array is large enough to address mp.index
            if (mp.index >= static_cast<int32_t>(mapPtr->polys.size()))
            {
                mapPtr->polys.resize(static_cast<size_t>(mp.index) + 1);
            }

            mapPtr->polys[mp.index].verts = mp.data.verts;
        }
    }

    void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        assert(mesh && "UndoRemoveVertex::redo: mesh is null");
        assert(mesh_data && "UndoRemoveVertex::redo: mesh_data is null");
        assert(vert_index >= 0 && "UndoRemoveVertex::redo: invalid vert_index");

        // Swap out poly vert lists back to the "removed-vertex" version captured in p.data.verts.
        // This is what makes redo fast and keeps undo data intact.
        for (SysFullPoly& p : polys)
        {
            assert(p.index >= 0 && "UndoRemoveVertex::redo: invalid poly index");
            assert(mesh->poly_valid(p.index) && "UndoRemoveVertex::redo: poly index no longer valid");

            mesh_data->polys[p.index].verts.swap(p.data.verts);
        }

        // Same swap for map polys (only if map still exists).
        for (SysFullMapPoly& mp : map_polys)
        {
            assert(mp.map >= 0 && "UndoRemoveVertex::redo: invalid map index");
            assert(mp.index >= 0 && "UndoRemoveVertex::redo: invalid map poly index");
            assert(mp.map < static_cast<int32_t>(mesh_data->mesh_maps.size()) &&
                   "UndoRemoveVertex::redo: map index out of range");

            auto& mapPtr = mesh_data->mesh_maps[mp.map];
            if (!mapPtr)
            {
                // Map was removed after this action was recorded.
                // Robust behavior: skip instead of crashing.
                continue;
            }

            if (mp.index >= static_cast<int32_t>(mapPtr->polys.size()))
            {
                // If the map poly array was resized down (unlikely), we can't swap safely.
                // Resize up so swap is legal.
                mapPtr->polys.resize(static_cast<size_t>(mp.index) + 1);
            }

            mapPtr->polys[mp.index].verts.swap(mp.data.verts);
        }

        // Finally remove the vertex (this will also record selection changes etc. only if not busy).
        mesh->remove_vert(vert_index);
        assert(!mesh->vert_valid(vert_index) && "UndoRemoveVertex::redo: vertex still valid after remove_vert");
    }

    std::vector<SysFullPoly>    polys{};
    std::vector<SysFullMapPoly> map_polys{};
    glm::vec3                   vert_pos{};
    SysMeshData*                mesh_data{nullptr};
    int                         vert_index{-1};
};

// -------------------------------------------------------------------------------

struct UndoCreatePoly : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->remove_poly(poly.index);
    }

    virtual void redo(void* data) override
    {
        SysMesh*             mesh      = static_cast<SysMesh*>(data);
        [[maybe_unused]] int new_index = mesh->create_poly(poly.data.verts, poly.data.material_id);
        assert(new_index == poly.index);
    }

    SysFullPoly poly;
};

// -------------------------------------------------------------------------------

struct UndoRemovePoly : public HistoryAction
{
    UndoRemovePoly() : removed(true)
    {
    }

    virtual void undo(void* data) override
    {
        assert(removed);
        SysMesh*             mesh      = static_cast<SysMesh*>(data);
        [[maybe_unused]] int new_index = mesh->create_poly(poly.data.verts, poly.data.material_id);
        assert(new_index == poly.index);
        removed = false;
    }

    virtual void redo(void* data) override
    {
        assert(!removed);
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->remove_poly(poly.index);
        removed = true;
    }

    bool        removed;
    SysFullPoly poly;
};

// -------------------------------------------------------------------------------

struct UndoSetPolyMaterial : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh*  mesh         = static_cast<SysMesh*>(data);
        const int new_material = mesh->poly_material(index);
        mesh->set_poly_material(index, old_material);
        old_material = new_material;
    }

    virtual void redo(void* data) override
    {
        undo(data);
    }

    int index;
    int old_material;
};

// -------------------------------------------------------------------------------

struct UndoMapMoveVertex : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh*     mesh = static_cast<SysMesh*>(data);
        const float* vec  = mesh->map_vert_position(vert.map, vert.index);
        float        new_pos[4];
        memcpy(new_pos, vec, mesh->map_dim(vert.map) * sizeof(float));
        mesh->map_vertex_move(vert.map, vert.index, &vert.data.vec[0]);
        memcpy(&vert.data.vec[0], new_pos, mesh->map_dim(vert.map) * sizeof(float));
    }

    virtual void redo(void* data) override
    {
        undo(data);
    }

    SysFullMapVert vert;
};

// -------------------------------------------------------------------------------

struct UndoMapRemovePoly : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);

        // if (!mesh->poly_valid(poly.index))
        // {
        //     std::cerr << "[UndoMapRemovePoly] Skipped undo — poly " << poly.index << " missing.\n";
        //     return;
        // }
        mesh->map_create_poly(poly.map, poly.index, poly.data.verts);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);

        // If the base polygon no longer exists, we can't remove its map poly
        // if (!mesh->poly_valid(poly.index))
        // {
        //     // Optional debug log:
        //     std::cerr << "[UndoMapRemovePoly] Skipped redo — base poly "
        //               << poly.index << " no longer exists.\n";
        //     return;
        // }
        assert(mesh->poly_valid(poly.index) && "Poly no longer exists in UndoMapRemovePoly::redo");
        assert(mesh->map_poly_valid(poly.map, poly.index) && "Map poly no longer valid in UndoMapRemovePoly::redo");

        mesh->map_remove_poly(poly.map, poly.index);
    }

    SysFullMapPoly poly;
};

// -------------------------------------------------------------------------------

struct UndoMapCreatePoly : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_remove_poly(poly.map, poly.index);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_create_poly(poly.map, poly.index, poly.data.verts);
        assert(mesh->poly_valid(poly.index));
    }

    SysFullMapPoly poly;
};

// -------------------------------------------------------------------------------

struct UndoMapCreateVertex : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_remove_vert(vert.map, vert.index);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        [[maybe_unused]] int32_t new_index = mesh->map_create_vert(vert.map, vert.data.vec);
        assert(new_index == vert.index);
    }

    SysFullMapVert vert;
};

// -------------------------------------------------------------------------------

struct UndoMapRemoveVertex : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        [[maybe_unused]] int32_t new_index = mesh->map_create_vert(vert.map, vert.data.vec);
        assert(new_index == vert.index);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_remove_vert(vert.map, vert.index);
    }

    SysFullMapVert vert;
};

// -------------------------------------------------------------------------------

struct UndoMapCreate : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_remove(id);
    }

    virtual void redo(void* data) override
    {
        SysMesh*             mesh      = static_cast<SysMesh*>(data);
        [[maybe_unused]] int new_index = mesh->map_create(id, type, dim);
        assert(index == new_index);
    }

    int index;
    int id;
    int type;
    int dim;
};

// -------------------------------------------------------------------------------

struct UndoMapRemove : public HistoryAction
{
    UndoMapRemove() : mesh_data(0), mesh_map(0), index(-1)
    {
    }

    ~UndoMapRemove()
    {
    }

    virtual void undo(void* /*data*/) override
    {
        [[maybe_unused]] int new_index = mesh_data->mesh_maps.insert(mesh_map);
        assert(new_index == index);
        mesh_map = 0;
    }

    virtual void redo(void* /*data*/) override
    {
        assert(mesh_data->mesh_maps[index] && !mesh_map);
        std::swap(mesh_map, mesh_data->mesh_maps[index]);
        mesh_data->mesh_maps.remove(index);
        assert(!mesh_data->mesh_maps[index] && mesh_map);
    }

    SysMeshData*                mesh_data;
    std::shared_ptr<SysMeshMap> mesh_map;
    int                         index;
};

// -------------------------------------------------------------------------------

struct UndoSelectMapVert : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_vert_select(map, index, !select);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->map_vert_select(map, index, select);
    }

    int  index;
    int  map;
    bool select;
};

// -------------------------------------------------------------------------------

struct UndoSelectVert : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->select_vert(index, !select);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->select_vert(index, select);
    }

    int  index;
    bool select;
};

// -------------------------------------------------------------------------------

struct UndoSelectEdge : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->select_edge(edge, !select);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->select_edge(edge, select);
    }

    IndexPair edge;
    bool      select;
};

// -------------------------------------------------------------------------------

struct UndoSelectPoly : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->select_poly(index, !select);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->select_poly(index, select);
    }

    int  index;
    bool select;
};

// -------------------------------------------------------------------------------

struct UndoClearVertSel : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        for (int index : sel)
            mesh->select_vert(index, true);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->clear_selected_verts();
    }

    std::vector<int> sel;
};

// -------------------------------------------------------------------------------

struct UndoClearEdgeSel : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        for (auto pair : sel)
            mesh->select_edge(pair, true);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->clear_selected_edges();
    }

    std::vector<std::pair<int, int>> sel;
};

// -------------------------------------------------------------------------------

struct UndoClearPolySel : public HistoryAction
{
    virtual void undo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        for (int index : sel)
            mesh->select_poly(index, true);
    }

    virtual void redo(void* data) override
    {
        SysMesh* mesh = static_cast<SysMesh*>(data);
        mesh->clear_selected_polys();
    }

    std::vector<int> sel;
};

#endif // SYS_HISTORY_ACTIONS_HPP_INCLUDED
