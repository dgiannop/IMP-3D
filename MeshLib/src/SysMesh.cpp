//
//  SysMesh.cpp
//  Mesh
//
//  Created by Daniel Giannopoulos on 6/20/15.
//  Copyright © 2015 Daniel Giannopoulos. All rights reserved.
//

#include "SysMesh.hpp"

#include <cstring>
#include <glm/glm.hpp>
#include <unordered_set>

#include "SysHistoryActions.hpp"
#include "SysMeshData.hpp"

SysMesh::SysMesh() : data{std::make_shared<SysMeshData>()}
{
    data->history_busy = false;
    data->history      = std::make_unique<History>(this, &data->history_busy);
}

SysMesh::SysMesh(SysMesh&& other) noexcept
    : data{std::move(other.data)}
{
}

SysMesh& SysMesh::operator=(SysMesh&& other) noexcept
{
    if (this != &other)
        data = std::move(other.data);
    return *this;
}

SysMesh::~SysMesh() = default;

void SysMesh::clear()
{
    // Geometry and topology
    data->verts.clear();
    data->polys.clear();

    // Maps
    data->mesh_maps.clear();

    // Selections
    data->edge_selection.clear();
    data->edge_selection_set.clear();
    data->vert_selection.clear();
    data->poly_selection.clear();

    // History
    if (data->history)
    {
        // Safe and fast way to release all undoable actions
        data->history->freeze();
    }

    // Invalidate counters
    data->topology_counter->change(); // mesh structure changed
    data->deform_counter->change();   // vertex positions cleared
    data->select_counter->change();   // selections cleared
}

History* SysMesh::history() const noexcept
{
    return data->history.get();
}

std::unique_ptr<History> SysMesh::release_history()
{
    // Move out the current history (transaction so far)
    std::unique_ptr<History> h = std::move(data->history);

    // Replace with a fresh one wired to the same busy guard
    data->history = std::make_unique<History>(this, &data->history_busy);

    return h;
}

const std::vector<int32_t>& SysMesh::all_verts() const noexcept
{
    return data->verts.valid_indices();
}

uint32_t SysMesh::num_verts() const noexcept
{
    return data->verts.size();
}

uint32_t SysMesh::vert_buffer_size() const noexcept
{
    return data->verts.capacity();
}

int32_t SysMesh::create_vert(const glm::vec3& pos) noexcept
{
    SysVert new_vert{};
    new_vert.pos         = pos;
    const int vert_index = data->verts.insert(new_vert);

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoCreateVertex>();
        undo->vert_index = vert_index;
        undo->vert_pos   = pos;
        data->history->insert(std::move(undo));
    }

    // Increment change counter.
    data->topology_counter->change();
    return vert_index;
}

int32_t SysMesh::clone_vert(int32_t vert_index, const glm::vec3& pos) noexcept
{
    const int32_t new_vert = create_vert(pos);
    if (data->verts[vert_index].selected)
    {
        select_vert(new_vert, true);
    }
    return new_vert;
}

void SysMesh::remove_vert(int32_t vert_index) noexcept
{
    assert(vert_valid(vert_index) && "Invalid vertex index!");
    assert(!data->verts[vert_index].removed && "Vertex has already been removed!");

    // Remove selection so history can record it consistently.
    select_vert(vert_index, false);

    // ---------------------------------------------------------
    // Capture undo BEFORE any mutation
    // ---------------------------------------------------------
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoRemoveVertex>();
        undo->vert_pos   = data->verts[vert_index].pos;
        undo->vert_index = vert_index;
        undo->mesh_data  = data.get();

        // IMPORTANT: copy, because later operations may mutate polys/verts adjacency
        const SysVertPolys affected_polys = data->verts[vert_index].polys;

        for (int32_t poly_index : affected_polys)
        {
            assert(poly_valid(poly_index) && "Affected poly is not valid!");
            assert(!data->polys[poly_index].removed);

            // Capture base polygon (full snapshot)
            SysFullPoly full_poly;
            full_poly.index = poly_index;
            full_poly.data  = data->polys[poly_index];

            // If SysPoly stores verts in a SmallList, ensure we snapshot the verts too.
            full_poly.data.verts.clear();
            for (int32_t v : data->polys[poly_index].verts)
                full_poly.data.verts.push_back(v);

            undo->polys.push_back(full_poly);

            // Capture map polygons (snapshot ONLY — no per-map history action)
            for (int32_t map = 0; map < static_cast<int32_t>(data->mesh_maps.size()); ++map)
            {
                if (!data->mesh_maps[map])
                    continue;

                if (!map_poly_valid(map, poly_index))
                    continue;

                SysFullMapPoly fmp;
                fmp.map        = map;
                fmp.index      = poly_index;
                fmp.data       = data->mesh_maps[map]->polys[poly_index];
                fmp.data.verts = data->mesh_maps[map]->polys[poly_index].verts; // snapshot verts
                undo->map_polys.push_back(fmp);
            }
        }

        data->history->insert(std::move(undo));
    }

    // ---------------------------------------------------------
    // Mutate mesh
    // ---------------------------------------------------------
    // Copy for safe iteration (remove_poly can change adjacency)
    const SysVertPolys affected_polys = data->verts[vert_index].polys;

    for (int32_t poly_index : affected_polys)
    {
        if (!poly_valid(poly_index))
            continue;

        // Find local index of the vertex in this polygon
        const int32_t face_index = data->polys[poly_index].verts.find_index(vert_index);
        // find_index(data->polys[poly_index].verts, vert_index);
        if (face_index < 0)
            continue;

        // Remove from base polygon
        SysPolyVerts& pv = data->polys[poly_index].verts;
        pv.erase(pv.begin() + face_index);

        // Remove from each map polygon (same face_index)
        for (int32_t map = 0; map < static_cast<int32_t>(data->mesh_maps.size()); ++map)
        {
            if (!data->mesh_maps[map])
                continue;

            if (!map_poly_valid(map, poly_index))
                continue;

            SysPolyVerts& map_pv = data->mesh_maps[map]->polys[poly_index].verts;

            // Defensive: map poly should match base poly size BEFORE erase
            if (face_index < 0 || face_index >= static_cast<int32_t>(map_pv.size()))
                continue;

            map_vert_select(map, map_pv[face_index], false);
            map_pv.erase(map_pv.begin() + face_index);
        }

        // If polygon now degenerate, remove it (this should also handle map polys as appropriate)
        if (data->polys[poly_index].verts.size() <= 2)
            remove_poly(poly_index);
    }

    // Remove the vertex
    data->verts[vert_index].removed = true;
    data->verts.remove(vert_index);

    data->topology_counter->change();
}

void SysMesh::move_vert(int32_t vert_index, const glm::vec3& new_pos) noexcept
{
    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoMoveVertex>();
        undo->vert_index = vert_index;
        undo->old_pos    = vert_position(vert_index);
        data->history->insert(std::move(undo));
    }

    // Update position and mark dirty.
    data->verts[vert_index].pos      = new_pos;
    data->verts[vert_index].modified = true;

    // Increment change counter.
    data->deform_counter->change();
}

const glm::vec3& SysMesh::vert_position(int32_t vert_index) const noexcept
{
    return data->verts[vert_index].pos;
}

const SysVertPolys& SysMesh::vert_polys(int32_t vert_index) const noexcept
{
    return data->verts[vert_index].polys;
}

SysVertEdges SysMesh::vert_edges(int32_t vert_index) const noexcept
{
    SysVertEdges result = {};
    for (int32_t poly : vert_polys(vert_index))
    {
        for (IndexPair edge : poly_edges(poly))
        {
            if (edge.first == vert_index || edge.second == vert_index)
            {
                // Keep face winding direction; do NOT sort here.
                result.insert_unique(edge);
            }
        }
    }
    return result;
}

bool SysMesh::boundary_vert(int32_t vert_index) const noexcept
{
    // boundary if any incident undirected edge has only one adjacent poly
    SysVertEdges undirected = {};
    for (IndexPair e : vert_edges(vert_index))
        undirected.insert_unique(sort_edge(e));

    // Each internal undirected edge incident to this vert should have 2 adjacent polys.
    // So boundary exists if any has < 2.
    for (IndexPair e : undirected)
        if (edge_polys(e).size() < 2)
            return true;

    return false;
}

bool SysMesh::outline_vert(int32_t vert_index) const noexcept
{
    for (int32_t poly_index : vert_polys(vert_index))
    {
        if (!poly_selected(poly_index))
        {
            return true;
        }
    }
    return false;
}

/// -------------------------------------------------------
/// Edges
/// -------------------------------------------------------

std::vector<IndexPair> SysMesh::all_edges() const noexcept
{
    std::vector<IndexPair> edges;
    edges.reserve(static_cast<size_t>(num_polys()) * 4);

    for (int32_t poly_index : all_polys())
    {
        for (IndexPair e : poly_edges(poly_index))
        {
            if (e.first > e.second)
                std::swap(e.first, e.second);

            edges.push_back(e);
        }
    }

    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    return edges;
}

uint32_t SysMesh::num_edges() const noexcept
{
    std::vector<IndexPair> edges;
    edges.reserve(static_cast<size_t>(num_polys()) * 4);

    for (int32_t poly : all_polys())
    {
        for (IndexPair e : poly_edges(poly))
        {
            if (e.first > e.second)
                std::swap(e.first, e.second);

            edges.push_back(e);
        }
    }

    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    return static_cast<uint32_t>(edges.size());
}

SysEdgePolys SysMesh::edge_polys(const IndexPair& edge) const noexcept
{
    SysEdgePolys   results = {};
    const SysVert& vert    = data->verts[edge.first]; // use edge.first as given

    for (int32_t poly_index : vert.polys)
        if (poly_has_edge(poly_index, edge))
            results.push_back(poly_index);

    return results;
}

bool SysMesh::boundary_edge(const IndexPair& edge) const noexcept
{
    return edge_polys(edge).size() == 1;
}

bool SysMesh::outline_edge(const IndexPair& edgeIn) const noexcept
{
    const IndexPair edge = sort_edge(edgeIn);

    if (!vert_valid(edge.first) || !vert_valid(edge.second))
        return false;

    int count = 0;
    for (int pid : edge_polys(edge))
        if (poly_valid(pid) && poly_selected(pid))
            ++count;

    return count == 1;
}

/// -------------------------------------------------------
/// Polys
/// -------------------------------------------------------

const std::vector<int32_t>& SysMesh::all_polys() const noexcept
{
    return data->polys.valid_indices();
}

uint32_t SysMesh::num_polys() const noexcept
{
    return data->polys.size();
}

uint32_t SysMesh::poly_buffer_size() const noexcept
{
    return data->polys.capacity();
}

int32_t SysMesh::create_poly(const SysPolyVerts& verts, uint32_t material_id) noexcept
{
    SysPoly new_poly{};
    new_poly.verts           = verts;
    new_poly.material_id     = material_id;
    const int32_t poly_index = data->polys.insert(new_poly);

    if (poly_index == data->polys.capacity() - 1)
    {
        for (int32_t i = 0; i < data->mesh_maps.size(); ++i)
        {
            if (data->mesh_maps[i])
            {
                data->mesh_maps[i]->polys.resize(data->polys.capacity());
            }
        }
    }

    assert(!data->polys[poly_index].removed);

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoCreatePoly>();
        undo->poly.index = poly_index;
        undo->poly.data  = new_poly;
        data->history->insert(std::move(undo));
    }

    // Add the new polygon to its vertices.
    for (int32_t vert_index : verts)
    {
        data->verts[vert_index].polys.insert_unique(poly_index);
    }

    data->topology_counter->change();
    return poly_index;
}

int32_t SysMesh::clone_poly(int32_t poly_index, const SysPolyVerts& new_verts) noexcept
{
    int32_t new_poly = create_poly(new_verts, data->polys[poly_index].material_id);
    if (data->polys[poly_index].selected)
    {
        select_poly(new_poly, true);
    }
    return new_poly;
}

void SysMesh::remove_poly(int32_t poly_index) noexcept
{
    assert(!data->polys[poly_index].removed && "Polygon has already been removed!");

    select_poly(poly_index, false);

    auto remove_edge_data = [&](IndexPair& edge) {
        select_edge(edge, false);
    };

    // If any of the polygon's edges only have this polygon connected, then
    // such edges are also about to be removed. Deselect any edge data for
    // such prior to removing the polygon so that the history will record it.
    for (IndexPair edge : poly_edges(poly_index))
    {
        if (edge_polys(edge).size() == 1)
        {
            remove_edge_data(edge);
        }
    }

    // Remove mapped polygons.
    for (int j = 0; j < data->mesh_maps.size(); ++j)
    {
        if (data->mesh_maps[j] && map_poly_valid(j, poly_index))
            map_remove_poly(j, poly_index);
    }

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoRemovePoly>();
        undo->poly.index = poly_index;
        undo->poly.data  = data->polys[poly_index];
        data->history->insert(std::move(undo));
    }

    // Remove polygon from its vertices.
    for (int32_t vert_index : data->polys[poly_index].verts)
    {
        data->verts[vert_index].polys.erase_element(poly_index);
    }

    data->polys[poly_index].removed = true;
    data->polys.remove(poly_index);
    data->topology_counter->change();
}

uint32_t SysMesh::poly_material(int32_t poly_index) const noexcept
{
    return data->polys[poly_index].material_id;
}

void SysMesh::set_poly_material(int32_t poly_index, uint32_t material_id) noexcept
{
    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo          = std::make_unique<UndoSetPolyMaterial>();
        undo->index        = poly_index;
        undo->old_material = data->polys[poly_index].material_id;
        data->history->insert(std::move(undo));
    }

    data->polys[poly_index].material_id = material_id;
    data->topology_counter->change();
}

bool SysMesh::poly_has_edge(int32_t poly_index, const IndexPair& edge) const noexcept
{
    const SysPoly&      poly = data->polys[poly_index];
    const SysPolyVerts& pv   = poly.verts;

    for (int32_t i = 0; i < pv.size(); ++i)
    {
        int a = pv[i];
        int b = pv[(i + 1) % pv.size()];

        if ((a == edge.first && b == edge.second) ||
            (a == edge.second && b == edge.first))
        {
            return true;
        }
    }
    return false;
}

const SysPolyVerts& SysMesh::poly_verts(int32_t poly_index) const noexcept
{
    assert(!data->polys[poly_index].removed && "nth polygon does not exist!");
    return data->polys[poly_index].verts;
}

SysPolyEdges SysMesh::poly_edges(int32_t poly_index) const noexcept
{
    SysPolyEdges   results;
    const SysPoly& poly = data->polys[poly_index];
    for (int prev = poly.verts.size() - 1, next = 0; next < poly.verts.size(); prev = next++)
    {
        results.push_back(IndexPair(poly.verts[prev], poly.verts[next]));
    }
    return results;
}

glm::vec3 SysMesh::poly_normal(int32_t poly_index) const noexcept
{
    assert(!data->polys[poly_index].removed && "nth polygon does not exist!");
    const SysPoly& poly = data->polys[poly_index];
    glm::vec3      norm(0.f);
    for (int prev = poly.verts.size() - 1, next = 0; next < poly.verts.size(); prev = next++)
    {
        const glm::vec3& prev_pos = data->verts[poly.verts[prev]].pos;
        const glm::vec3& next_pos = data->verts[poly.verts[next]].pos;
        norm[0] += (prev_pos[1] - next_pos[1]) * (prev_pos[2] + next_pos[2]);
        norm[1] += (prev_pos[2] - next_pos[2]) * (prev_pos[0] + next_pos[0]);
        norm[2] += (prev_pos[0] - next_pos[0]) * (prev_pos[1] + next_pos[1]);
    }
    // return glm::normalize(norm);
    // Safe normalize (no NaNs)
    const float len2 = glm::dot(norm, norm);
    if (len2 < 1e-20f)
        return glm::vec3(0.0f);

    return norm / std::sqrt(len2);
}

glm::vec3 SysMesh::poly_center(int32_t poly_index) const noexcept
{
    const SysPolyVerts& pv = poly_verts(poly_index);
    glm::vec3           pos(0.f);
    for (int32_t i = 0; i < pv.size(); ++i)
    {
        pos += vert_position(pv[i]);
    }
    return pos / static_cast<float>(pv.size());
}

/// -------------------------------------------------------
/// Maps
/// -------------------------------------------------------

int32_t SysMesh::map_create(int32_t id, int32_t type, int32_t dim) noexcept
{
    auto new_map  = std::make_shared<SysMeshMap>();
    new_map->id   = id;
    new_map->type = type;
    new_map->dim  = dim;

    // Make the number of polygons in the map match the mesh.
    new_map->polys.resize(data->polys.capacity(), SysMapPoly());

    int32_t index = data->mesh_maps.insert(new_map);

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo   = std::make_unique<UndoMapCreate>();
        undo->index = index;
        undo->id    = id;
        undo->type  = type;
        undo->dim   = dim;
        data->history->insert(std::move(undo));
    }

    data->topology_counter->change();
    return index;
}

bool SysMesh::map_remove(int32_t id) noexcept
{
    int32_t map = map_find(id);
    if (map == -1)
        return false;

    const bool busy = data->history->is_busy();

    if (!busy)
    {
        // Record undo so we can put the same map object back later.
        auto undo       = std::make_unique<UndoMapRemove>();
        undo->mesh_data = data.get();
        undo->mesh_map  = data->mesh_maps[map]; // keep the object alive
        undo->index     = map;
        data->history->insert(std::move(undo));

        // (Optional) clear selection while the object is still valid.
        if (data->mesh_maps[map])
            data->mesh_maps[map]->selection.clear();
    }
    else
    {
        // During replay: do NOT touch the map after resetting.
        data->mesh_maps[map].reset();
    }

    // Remove the slot from the hole list (marks index free).
    data->mesh_maps.remove(map);

    data->topology_counter->change();
    return true;
}

int32_t SysMesh::map_find(int32_t id) const noexcept
{
    for (int32_t i = 0; i < data->mesh_maps.size(); ++i)
    {
        if (data->mesh_maps[i] && data->mesh_maps[i]->id == id)
            return i;
    }
    return -1;
}

int32_t SysMesh::map_dim(int32_t map) const noexcept
{
    return data->mesh_maps[map]->dim;
}

void SysMesh::map_create_poly(int32_t map, int32_t poly_index, const SysPolyVerts& pv) noexcept
{
    assert(!map_poly_valid(map, poly_index) && "The polygon already exists!");
    assert(data->polys[poly_index].verts.size() == pv.size() &&
           "The number of vertices for the map polygon must match the mesh polygon!");

    data->mesh_maps[map]->polys[poly_index].verts = pv;

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoMapCreatePoly>();
        undo->poly.data  = data->mesh_maps[map]->polys[poly_index];
        undo->poly.index = poly_index;
        undo->poly.map   = map;
        data->history->insert(std::move(undo));
    }
    data->topology_counter->change();
}

int32_t SysMesh::map_create_vert(int32_t map, const float* vec) noexcept
{
    SysMeshMap& mesh_map = *data->mesh_maps[map];
    SysMapVert  new_vert{};

    memcpy(new_vert.vec, vec, mesh_map.dim * sizeof(float));

    int32_t index = static_cast<int32_t>(data->mesh_maps[map]->verts.size());
    // Check if there is free vert we can use, else add a new one.
    if (mesh_map.free_verts.empty())
        mesh_map.verts.push_back(new_vert);
    else
    {
        index = mesh_map.free_verts.back();
        mesh_map.free_verts.pop_back();
        data->mesh_maps[map]->verts[index] = new_vert;
    }

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo = std::make_unique<UndoMapCreateVertex>();
        memcpy(undo->vert.data.vec,
               data->mesh_maps[map]->verts[index].vec,
               mesh_map.dim * sizeof(float));
        undo->vert.index = index;
        undo->vert.map   = map;
        data->history->insert(std::move(undo));
    }

    data->topology_counter->change();
    return index;
}

void SysMesh::map_remove_vert(int32_t map, int32_t vert_index) noexcept
{
    SysMeshMap& mesh_map = *data->mesh_maps[map];
    assert(!mesh_map.verts[vert_index].removed && "Vertex does not exist!");

    map_vert_select(map, vert_index, false);

    mesh_map.free_verts.push_back(vert_index);
    mesh_map.verts[vert_index].removed = true;

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo = std::make_unique<UndoMapRemoveVertex>();
        memcpy(undo->vert.data.vec,
               data->mesh_maps[map]->verts[vert_index].vec,
               mesh_map.dim * sizeof(float));
        undo->vert.index = vert_index;
        undo->vert.map   = map;
        data->history->insert(std::move(undo));
    }

    data->topology_counter->change();
}

bool SysMesh::map_poly_valid(int32_t map, int32_t poly_index) const noexcept
{
    // Bounds / existence checks first.
    if (map < 0 || map >= static_cast<int32_t>(data->mesh_maps.size()))
        return false;

    const auto& mp = data->mesh_maps[map];
    if (!mp)
        return false;

    // Poly index must be in range and not removed.
    if (!poly_valid(poly_index))
        return false;

    // Map polys array must contain this slot (no resizing in a const query).
    if (poly_index < 0 || poly_index >= static_cast<int32_t>(mp->polys.size()))
        return false;

    return !mp->polys[poly_index].verts.empty();
}

void SysMesh::map_remove_poly(int32_t map, int32_t poly_index)
{
    for (int32_t vert_index : data->mesh_maps[map]->polys[poly_index].verts)
    {
        map_vert_select(map, vert_index, false);
    }

    // Add undo entry.
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoMapRemovePoly>();
        undo->poly.data  = data->mesh_maps[map]->polys[poly_index];
        undo->poly.index = poly_index;
        undo->poly.map   = map;
        data->history->insert(std::move(undo));
    }

    assert(map_poly_valid(map, poly_index) && "Trying to remove an invalid polygon!");

    data->mesh_maps[map]->polys[poly_index].verts.clear();
    data->topology_counter->change();
}

void SysMesh::map_vertex_move(int32_t map, int32_t vert_index, const float* new_vec) noexcept
{
    if (!data->history->is_busy())
    {
        auto undo = std::make_unique<UndoMapMoveVertex>();
        std::memcpy(undo->vert.data.vec,
                    data->mesh_maps[map]->verts[vert_index].vec,
                    data->mesh_maps[map]->dim * sizeof(float));
        undo->vert.index = vert_index;
        undo->vert.map   = map;
        data->history->insert(std::move(undo));
    }

    std::memcpy(data->mesh_maps[map]->verts[vert_index].vec,
                new_vec,
                data->mesh_maps[map]->dim * sizeof(float));
    data->deform_counter->change();
}

const SysPolyVerts& SysMesh::map_poly_verts(int32_t map, int32_t poly) const
{
    assert(!data->polys[poly].removed && "nth polygon does not exist!");
    return data->mesh_maps[map]->polys[poly].verts;
}

const float* SysMesh::map_vert_position(int32_t map, int32_t n) const noexcept
{
    return data->mesh_maps[map]->verts[n].vec;
}

uint32_t SysMesh::map_buffer_size(int32_t map) const noexcept
{
    return static_cast<uint32_t>(data->mesh_maps[map]->verts.size());
}

/// -------------------------------------------------------
/// Selection
/// -------------------------------------------------------

bool SysMesh::select_vert(int32_t vert_index, bool select) noexcept
{
    if (data->verts[vert_index].selected != select)
    {
        // Add undo entry.
        if (!data->history->is_busy())
        {
            auto undo    = std::make_unique<UndoSelectVert>();
            undo->index  = vert_index;
            undo->select = select;
            data->history->insert(std::move(undo));
        }

        data->verts[vert_index].selected = select;

        if (select)
        {
            data->vert_selection.push_back(vert_index);
        }
        else
        {
            auto it = std::find(data->vert_selection.begin(), data->vert_selection.end(), vert_index);
            if (it != data->vert_selection.end())
            {
                data->vert_selection.erase(it);
            }
        }

        data->select_counter->change();
        return true;
    }
    return false;
}

bool SysMesh::vert_selected(int32_t vert_index) const noexcept
{
    return data->verts[vert_index].selected;
}

const std::vector<int32_t>& SysMesh::selected_verts() const noexcept
{
    return data->vert_selection;
}

void SysMesh::clear_selected_verts() noexcept
{
    if (!data->vert_selection.empty())
    {
        // Add undo entry.
        if (!data->history->is_busy())
        {
            auto undo = std::make_unique<UndoClearVertSel>();
            undo->sel = data->vert_selection;
            data->history->insert(std::move(undo));
        }

        for (int32_t vert_index : data->vert_selection)
        {
            data->verts[vert_index].selected = false;
        }
        data->vert_selection.clear();
        data->select_counter->change();
    }
}

bool SysMesh::select_poly(int poly_index, bool select) noexcept
{
    if (data->polys[poly_index].selected != select)
    {
        // Add undo entry.
        if (!data->history->is_busy())
        {
            auto undo    = std::make_unique<UndoSelectPoly>();
            undo->index  = poly_index;
            undo->select = select;
            data->history->insert(std::move(undo));
        }

        data->polys[poly_index].selected = select;

        if (select)
        {
            data->poly_selection.push_back(poly_index);
        }
        else
        {
            auto it = std::find(data->poly_selection.begin(), data->poly_selection.end(), poly_index);
            if (it != data->poly_selection.end())
            {
                data->poly_selection.erase(it);
            }
        }

        data->select_counter->change();
        return true;
    }
    return false;
}

const std::vector<int32_t>& SysMesh::selected_polys() const noexcept
{
    return data->poly_selection;
}

bool SysMesh::poly_selected(int32_t poly_index) const noexcept
{
    return data->polys[poly_index].selected;
}

void SysMesh::clear_selected_polys() noexcept
{
    if (!data->poly_selection.empty())
    {
        // Add undo entry.
        if (!data->history->is_busy())
        {
            auto undo = std::make_unique<UndoClearPolySel>();
            undo->sel = data->poly_selection;
            data->history->insert(std::move(undo));
        }
        for (int32_t poly_index : data->poly_selection)
        {
            data->polys[poly_index].selected = false;
        }
        data->poly_selection.clear();
        data->select_counter->change();
    }
}

bool SysMesh::select_edge(const IndexPair& edgeIn, bool select) noexcept
{
    const IndexPair edge              = sort_edge(edgeIn);
    const bool      currentlySelected = data->edge_selection_set.contains(edge);

    if (select != currentlySelected)
    {
        if (!data->history->is_busy())
        {
            auto undo    = std::make_unique<UndoSelectEdge>();
            undo->edge   = edge; // store normalized
            undo->select = select;
            data->history->insert(std::move(undo));
        }

        if (select && data->edge_selection_set.insert(edge))
        {
            data->edge_selection.push_back(edge);
            data->select_counter->change();
            return true;
        }
        else if (!select && data->edge_selection_set.erase(edge))
        {
            std::erase_if(data->edge_selection, [&edge](const IndexPair& pair) {
                return SysMesh::sort_edge(pair) == edge;
            });
            data->select_counter->change();
            return true;
        }
    }
    return false;
}

bool SysMesh::edge_selected(const IndexPair& edgeIn) const noexcept
{
    return data->edge_selection_set.contains(sort_edge(edgeIn));
}

const std::vector<IndexPair>& SysMesh::selected_edges() const noexcept
{
    return data->edge_selection;
}

void SysMesh::clear_selected_edges() noexcept
{
    if (!data->edge_selection_set.empty())
    {
        // Add undo entry.
        if (!data->history->is_busy())
        {
            auto undo = std::make_unique<UndoClearEdgeSel>();
            undo->sel = data->edge_selection;
            data->history->insert(std::move(undo));
        }
        data->edge_selection_set.clear();
        data->edge_selection.clear();
        data->select_counter->change();
    }
}

void SysMesh::map_vert_select(int32_t map, int32_t vert_index, bool select) noexcept
{
    if (data->mesh_maps[map]->verts[vert_index].selected != select)
    {
        // Add undo entry.
        if (!data->history->is_busy())
        {
            auto undo    = std::make_unique<UndoSelectMapVert>();
            undo->index  = vert_index;
            undo->map    = map;
            undo->select = select;
            data->history->insert(std::move(undo));
        }

        data->mesh_maps[map]->verts[vert_index].selected = select;
        if (select)
        {
            data->mesh_maps[map]->selection.push_back(vert_index);
        }
        else
        {
            std::erase_if(data->mesh_maps[map]->selection, [&vert_index](int32_t v) {
                return (v == vert_index);
            });
        }

        data->select_counter->change();
    }
}

bool SysMesh::map_vert_selected(int32_t map, int32_t vert_index) const noexcept
{
    return data->mesh_maps[map]->verts[vert_index].selected;
}

const std::vector<int32_t>& SysMesh::selected_map_verts(int32_t map) const noexcept
{
    return data->mesh_maps[map]->selection;
}

void SysMesh::map_clear_selected_verts(int32_t map) noexcept
{
    // Undo??
    for (int32_t vert_index : data->mesh_maps[map]->selection)
    {
        data->mesh_maps[map]->verts[vert_index].selected = false;
    }
    data->mesh_maps[map]->selection.clear();
}

// -------------------------------------------------------------------------------

const SysCounterPtr& SysMesh::change_counter() const noexcept
{
    return data->change_counter;
}

// -------------------------------------------------------------------------------

const SysCounterPtr& SysMesh::topology_counter() const noexcept
{
    return data->topology_counter;
}

// -------------------------------------------------------------------------------

const SysCounterPtr& SysMesh::deform_counter() const noexcept
{
    return data->deform_counter;
}

// -------------------------------------------------------------------------------

const SysCounterPtr& SysMesh::select_counter() const noexcept
{
    return data->select_counter;
}

IndexPair SysMesh::sort_edge(const IndexPair& edge) noexcept
{
    return (edge.first < edge.second) ? edge : IndexPair{edge.second, edge.first};
}

bool SysMesh::poly_valid(int32_t poly_index) const noexcept
{
    return poly_index >= 0 && !data->polys[poly_index].removed;
}

bool SysMesh::vert_valid(int32_t vert_index) const noexcept
{
    return vert_index >= 0 && !data->verts[vert_index].removed;
}

/// Topology Traversal -------------------------------

namespace
{
    struct PairHash
    {
        size_t operator()(const IndexPair& p) const noexcept
        {
            // 64-bit mix (stable across platforms)
            const uint64_t a = static_cast<uint32_t>(p.first);
            const uint64_t b = static_cast<uint32_t>(p.second);
            return static_cast<size_t>((a << 32) ^ b);
        }
    };

    struct DirEdge
    {
        int32_t a{};
        int32_t b{};

        bool operator==(const DirEdge& o) const noexcept
        {
            return a == o.a && b == o.b;
        }
    };

    struct DirEdgeHash
    {
        size_t operator()(const DirEdge& e) const noexcept
        {
            const uint64_t a = static_cast<uint32_t>(e.a);
            const uint64_t b = static_cast<uint32_t>(e.b);
            return static_cast<size_t>((a << 32) ^ b);
        }
    };

    struct HalfRec
    {
        int32_t poly{};
        int32_t next{}; // for directed (a->b), next is the vertex after b in that poly
    };
} // namespace

std::vector<IndexPair> SysMesh::edge_loop(const IndexPair& seedIn) const
{
    const IndexPair seed = sort_edge(seedIn);

    if (!vert_valid(seed.first) || !vert_valid(seed.second))
        return {};

    struct PairHash
    {
        size_t operator()(const IndexPair& e) const noexcept
        {
            const uint64_t a = static_cast<uint32_t>(e.first);
            const uint64_t b = static_cast<uint32_t>(e.second);
            return std::hash<uint64_t>{}((a << 32) | b);
        }
    };

    auto undirected_edges_at_vert = [&](int32_t v) -> SysVertEdges {
        SysVertEdges out = {};
        for (IndexPair e : vert_edges(v))
            out.insert_unique(sort_edge(e));
        return out;
    };

    // In quad p, return the other edge in p that touches vertex v (besides cur).
    // If p not quad or cur not in p or v not on cur, returns {-1,-1}.
    auto face_adjacent_edge_at_vert = [&](int32_t p, const IndexPair& cur, int32_t v) -> IndexPair {
        if (!poly_valid(p))
            return IndexPair{-1, -1};

        const SysPolyVerts& pv = poly_verts(p);
        if (pv.size() != 4)
            return IndexPair{-1, -1};

        // Find cur as an undirected edge in this face (as consecutive verts).
        int32_t cur_i = -1;
        for (int32_t i = 0; i < 4; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) & 3];
            if (sort_edge(IndexPair{a, b}) == cur)
            {
                cur_i = i;
                break;
            }
        }
        if (cur_i < 0)
            return IndexPair{-1, -1};

        // cur is between pv[cur_i] and pv[cur_i+1]
        const int32_t a = pv[cur_i];
        const int32_t b = pv[(cur_i + 1) & 3];

        if (v != a && v != b)
            return IndexPair{-1, -1};

        // The face-adjacent edge at vertex v is:
        // - if v == a, then edge (pv[cur_i-1], a)
        // - if v == b, then edge (b, pv[cur_i+2])
        if (v == a)
        {
            const int32_t prev = pv[(cur_i + 3) & 3];
            return sort_edge(IndexPair{prev, a});
        }
        else
        {
            const int32_t next = pv[(cur_i + 2) & 3];
            return sort_edge(IndexPair{b, next});
        }
    };

    // Advance one step from current edge cur, walking at vertex "at".
    // Returns next undirected edge, or {-1,-1} to stop.
    auto next_edge_at_vertex = [&](const IndexPair& cur, int32_t at) -> IndexPair {
        if (!vert_valid(at))
            return IndexPair{-1, -1};

        // Need incident edges at vertex.
        const SysVertEdges inc = undirected_edges_at_vert(at);

        // Must contain current edge.
        bool hasCur = false;
        for (const IndexPair& e : inc)
            if (e == cur)
                hasCur = true;
        if (!hasCur)
            return IndexPair{-1, -1};

        // Determine "turn" edges: in each adjacent quad, the edge adjacent to cur at 'at'.
        std::unordered_set<IndexPair, PairHash> turn = {};

        const SysEdgePolys polys = edge_polys(cur);
        // If non-manifold (>2) or boundary (<2), we still *can* try, but ambiguity is likely.
        // We'll be conservative and require exactly 2 adjacent polys for a clean quad-loop.
        if (polys.size() != 2)
            return IndexPair{-1, -1};

        for (int32_t p : polys)
        {
            const IndexPair adj = face_adjacent_edge_at_vert(p, cur, at);
            if (adj.first >= 0)
                turn.insert(adj);
        }

        // Candidates = incident - cur - turnEdges
        IndexPair candidate{-1, -1};
        int32_t   count = 0;

        for (const IndexPair& e : inc)
        {
            if (e == cur)
                continue;
            if (turn.contains(e))
                continue;

            candidate = e;
            ++count;
            if (count > 1)
                break;
        }

        // Exactly one "straight" edge => continue.
        if (count == 1)
            return candidate;

        // Otherwise ambiguous (pole/extraordinary, non-quad topology around vertex, etc.)
        return IndexPair{-1, -1};
    };

    // Walk in one direction: starting from edge cur, we advance from 'from' -> 'to'
    // meaning we consider the "front" vertex as 'to'.
    auto walk = [&](int32_t from, int32_t to) -> std::vector<IndexPair> {
        std::vector<IndexPair> out;
        out.reserve(64);

        std::unordered_set<IndexPair, PairHash> visited;

        IndexPair cur = sort_edge(IndexPair{from, to});

        // Track the advancing vertex each step.
        int32_t at = to;

        for (;;)
        {
            if (!visited.insert(cur).second)
                break; // loop closed/cycle

            out.push_back(cur);

            const IndexPair nxt = next_edge_at_vertex(cur, at);
            if (nxt.first < 0)
                break;

            // Update advancing vertex:
            // nxt shares 'at' with cur; the other endpoint becomes the new 'at'.
            const int32_t a = nxt.first;
            const int32_t b = nxt.second;
            at              = (a == at) ? b : a;

            cur = nxt;
        }

        return out;
    };

    const std::vector<IndexPair> fwd = walk(seed.first, seed.second);
    const std::vector<IndexPair> bwd = walk(seed.second, seed.first);

    if (fwd.empty() && bwd.empty())
        return {};

    // Merge bidirectionally around seed.
    std::vector<IndexPair> result;
    result.reserve(bwd.size() + fwd.size());

    for (int32_t i = static_cast<int32_t>(bwd.size()) - 1; i >= 0; --i)
        result.push_back(bwd[static_cast<size_t>(i)]);

    if (!result.empty() && !fwd.empty() && result.back() == fwd.front())
        result.pop_back();

    for (const IndexPair& e : fwd)
        result.push_back(e);

    return result;
}

std::vector<IndexPair> SysMesh::edge_ring(const IndexPair& seedIn) const
{
    std::vector<IndexPair> result;

    const IndexPair seed = sort_edge(seedIn);
    if (!vert_valid(seed.first) || !vert_valid(seed.second))
        return result;

    const SysEdgePolys seedPolys = edge_polys(seed);
    if (seedPolys.empty())
        return result;

    auto poly_is_quad = [this](int32_t p) noexcept -> bool {
        return poly_valid(p) && (poly_verts(p).size() == 4);
    };

    auto find_edge_index_in_quad = [&, this](int32_t p, const IndexPair& e, int32_t& outI) noexcept -> bool {
        if (!poly_is_quad(p))
            return false;

        const SysPolyVerts& pv = poly_verts(p);
        // quad edges are (0-1),(1-2),(2-3),(3-0)
        for (int32_t i = 0; i < 4; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) & 3];
            if ((a == e.first && b == e.second) || (a == e.second && b == e.first))
            {
                outI = i;
                return true;
            }
        }
        return false;
    };

    auto opposite_edge_in_quad = [this](int32_t p, int32_t edgeIndex) noexcept -> IndexPair {
        const SysPolyVerts& pv = poly_verts(p);
        const int32_t       i2 = (edgeIndex + 2) & 3;
        const int32_t       a  = pv[i2];
        const int32_t       b  = pv[(i2 + 1) & 3];
        return sort_edge(IndexPair{a, b});
    };

    auto walk = [&](int32_t startPoly, const IndexPair& startEdge) -> std::vector<IndexPair> {
        std::vector<IndexPair> out;
        out.reserve(64);

        std::unordered_set<IndexPair, PairHash> visited;
        visited.reserve(128);

        IndexPair curEdge = sort_edge(startEdge);
        int32_t   curPoly = startPoly;

        // include seed
        out.push_back(curEdge);
        visited.insert(curEdge);

        while (true)
        {
            if (!poly_is_quad(curPoly))
                break;

            int32_t ei = -1;
            if (!find_edge_index_in_quad(curPoly, curEdge, ei))
                break;

            const IndexPair opp = opposite_edge_in_quad(curPoly, ei);
            if (!visited.insert(opp).second)
                break;

            out.push_back(opp);

            // Step across the opposite edge to the neighboring quad poly, then continue.
            const SysEdgePolys oppPolys = edge_polys(opp);

            // Require exactly 2 incident polys for manifold interior stepping.
            if (oppPolys.size() != 2)
                break;

            int32_t nextPoly = (oppPolys[0] == curPoly) ? oppPolys[1] : oppPolys[0];
            if (!poly_is_quad(nextPoly))
                break;

            // Advance: now the "current edge" is opp in nextPoly
            curEdge = opp;
            curPoly = nextPoly;
        }

        return out;
    };

    // Choose a deterministic starting poly for the seed (quad-only).
    int32_t seedPoly = -1;
    for (int32_t p : seedPolys)
    {
        if (poly_is_quad(p))
        {
            seedPoly = p;
            break;
        }
    }
    if (seedPoly < 0)
        return result;

    // Walk both directions by starting from the two possible seed-adjacent quads (if present).
    // For a manifold interior seed, this gives the complete ring on both sides.
    std::vector<IndexPair> aSide = walk(seedPoly, seed);

    // Try the other incident quad, if any, and merge uniquely.
    std::vector<IndexPair> bSide;
    for (int32_t p : seedPolys)
    {
        if (p != seedPoly && poly_is_quad(p))
        {
            bSide = walk(p, seed);
            break;
        }
    }

    std::unordered_set<IndexPair, PairHash> seen;
    seen.reserve((aSide.size() + bSide.size()) * 2);

    result.reserve(aSide.size() + bSide.size());
    for (const IndexPair& e : aSide)
        if (seen.insert(sort_edge(e)).second)
            result.push_back(sort_edge(e));
    for (const IndexPair& e : bSide)
        if (seen.insert(sort_edge(e)).second)
            result.push_back(sort_edge(e));

    return result;
}
