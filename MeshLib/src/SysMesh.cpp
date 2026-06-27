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

// --------------------------------------------------------------------------
// Construction / destruction
// --------------------------------------------------------------------------

SysMesh::SysMesh() : data{std::make_shared<SysMeshData>()}
{
    data->history_busy = false;
    data->history      = std::make_unique<History>(this, &data->history_busy);

    // Create the two permanent default maps under a busy guard so they never
    // appear in the undo stack. Every SysMesh always has these — tools can
    // rely on MESH_MAP_NORMALS (0) and MESH_MAP_UV0 (1) without map_find().
    data->history_busy = true;
    map_create(MESH_MAP_NORMALS, /*type=*/0, /*dim=*/3); // slot 0 — per-vertex normals
    map_create(MESH_MAP_UV0, /*type=*/1, /*dim=*/2);     // slot 1 — UV channel 0
    data->history_busy = false;
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
    // Geometry and topology.
    data->verts.clear();
    data->polys.clear();

    // Remove all non-default maps, then empty the default slots.
    // Default maps (MESH_MAP_NORMALS, MESH_MAP_UV0) are permanent — they
    // survive clear() so tools never need to check for their existence.
    for (int32_t i = data->mesh_maps.slot_count() - 1; i > MESH_MAP_UV0; --i)
    {
        if (data->mesh_maps.is_valid(i))
            data->mesh_maps.remove(i);
    }
    for (int32_t i = MESH_MAP_NORMALS; i <= MESH_MAP_UV0; ++i)
    {
        if (data->mesh_maps.is_valid(i) && data->mesh_maps[i])
        {
            data->mesh_maps[i]->verts.clear();
            data->mesh_maps[i]->polys.clear();
            data->mesh_maps[i]->selection.clear();
        }
    }

    // Selections.
    data->edge_selection.clear();
    data->edge_selection_set.clear();
    data->vert_selection.clear();
    data->poly_selection.clear();

    // History.
    if (data->history)
        data->history->freeze();

    // Invalidate counters.
    data->topology_counter->change();
    data->deform_counter->change();
    data->select_counter->change();
}

// --------------------------------------------------------------------------
// reserve() — call before bulk import with the expected vertex count.
// Polygon, edge, and selection buffer sizes are derived automatically.
// --------------------------------------------------------------------------
void SysMesh::reserve(int32_t vert_count) noexcept
{
    // Euler estimate: for a closed quad mesh F ≈ V, for tris F ≈ 2V.
    // vert_count is a safe upper bound for polys in the quad case.
    const int32_t poly_estimate = vert_count;
    const int32_t edge_estimate = vert_count * 2;

    data->verts.reserve(vert_count);
    data->polys.reserve(poly_estimate);

    data->vert_selection.reserve(vert_count);
    data->poly_selection.reserve(poly_estimate);
    data->edge_selection.reserve(edge_estimate);
    // data->edge_selection_set.reserve(edge_estimate); //TODO

    // Reserve the two default maps — always present so safe to access directly.
    for (int32_t i = MESH_MAP_NORMALS; i <= MESH_MAP_UV0; ++i)
    {
        if (data->mesh_maps.is_valid(i) && data->mesh_maps[i])
        {
            data->mesh_maps[i]->verts.reserve(vert_count);
            data->mesh_maps[i]->polys.reserve(poly_estimate);
        }
    }
}

History* SysMesh::history() const noexcept
{
    return data->history.get();
}

std::unique_ptr<History> SysMesh::release_history()
{
    std::unique_ptr<History> h = std::move(data->history);
    data->history              = std::make_unique<History>(this, &data->history_busy);
    return h;
}

// --------------------------------------------------------------------------
// Vertices
// --------------------------------------------------------------------------

const std::vector<int32_t>& SysMesh::all_verts() const noexcept
{
    return data->verts.valid_indices();
}

int32_t SysMesh::num_verts() const noexcept
{
    return data->verts.size();
}

int32_t SysMesh::vert_buffer_size() const noexcept
{
    return data->verts.slot_count();
}

int32_t SysMesh::create_vert(const glm::vec3& pos) noexcept
{
    SysVert new_vert{};
    new_vert.pos             = pos;
    const int32_t vert_index = data->verts.insert(new_vert);

    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoCreateVertex>();
        undo->vert_index = vert_index;
        undo->vert_pos   = pos;
        data->history->insert(std::move(undo));
    }

    data->topology_counter->change();
    return vert_index;
}

int32_t SysMesh::clone_vert(int32_t vert_index, const glm::vec3& pos) noexcept
{
    const int32_t new_vert = create_vert(pos);
    if (data->verts[vert_index].selected)
        select_vert(new_vert, true);
    return new_vert;
}

void SysMesh::remove_vert(int32_t vert_index) noexcept
{
    assert(vert_valid(vert_index) && "Invalid vertex index!");
    assert(!data->verts[vert_index].removed && "Vertex has already been removed!");

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

            SysFullPoly full_poly;
            full_poly.index = poly_index;
            full_poly.data  = data->polys[poly_index];

            full_poly.data.verts.clear();
            for (int32_t v : data->polys[poly_index].verts)
                full_poly.data.verts.push_back(v);

            undo->polys.push_back(full_poly);

            for (int32_t map = 0; map < data->mesh_maps.slot_count(); ++map)
            {
                if (!data->mesh_maps.is_valid(map) || !data->mesh_maps[map])
                    continue;

                if (!map_poly_valid(map, poly_index))
                    continue;

                SysFullMapPoly fmp;
                fmp.map        = map;
                fmp.index      = poly_index;
                fmp.data       = data->mesh_maps[map]->polys[poly_index];
                fmp.data.verts = data->mesh_maps[map]->polys[poly_index].verts;
                undo->map_polys.push_back(fmp);
            }
        }

        data->history->insert(std::move(undo));
    }

    // ---------------------------------------------------------
    // Mutate mesh
    // ---------------------------------------------------------
    const SysVertPolys affected_polys = data->verts[vert_index].polys;

    for (int32_t poly_index : affected_polys)
    {
        if (!poly_valid(poly_index))
            continue;

        const int32_t face_index = data->polys[poly_index].verts.find_index(vert_index);
        if (face_index < 0)
            continue;

        SysPolyVerts& pv = data->polys[poly_index].verts;
        pv.erase(pv.begin() + face_index);

        for (int32_t map = 0; map < data->mesh_maps.slot_count(); ++map)
        {
            if (!data->mesh_maps.is_valid(map) || !data->mesh_maps[map])
                continue;

            if (!map_poly_valid(map, poly_index))
                continue;

            SysPolyVerts& map_pv = data->mesh_maps[map]->polys[poly_index].verts;

            if (face_index >= static_cast<int32_t>(map_pv.size()))
                continue;

            map_vert_select(map, map_pv[face_index], false);
            map_pv.erase(map_pv.begin() + face_index);
        }

        if (data->polys[poly_index].verts.size() <= 2)
            remove_poly(poly_index);
    }

    data->verts[vert_index].removed = true;
    data->verts.remove(vert_index);
    data->topology_counter->change();
}

void SysMesh::move_vert(int32_t vert_index, const glm::vec3& new_pos) noexcept
{
    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoMoveVertex>();
        undo->vert_index = vert_index;
        undo->old_pos    = vert_position(vert_index);
        data->history->insert(std::move(undo));
    }

    data->verts[vert_index].pos      = new_pos;
    data->verts[vert_index].modified = true;
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
        for (IndexPair edge : poly_edges(poly))
            if (edge.first == vert_index || edge.second == vert_index)
                result.insert_unique(edge);
    return result;
}

bool SysMesh::boundary_vert(int32_t vert_index) const noexcept
{
    SysVertEdges undirected = {};
    for (IndexPair e : vert_edges(vert_index))
        undirected.insert_unique(sort_edge(e));

    for (IndexPair e : undirected)
        if (edge_polys(e).size() < 2)
            return true;

    return false;
}

bool SysMesh::outline_vert(int32_t vert_index) const noexcept
{
    for (int32_t poly_index : vert_polys(vert_index))
        if (!poly_selected(poly_index))
            return true;
    return false;
}

bool SysMesh::vert_valid(int32_t vert_index) const noexcept
{
    return vert_index >= 0 && data->verts.is_valid(vert_index) && !data->verts[vert_index].removed;
}

// --------------------------------------------------------------------------
// Edges
// --------------------------------------------------------------------------

std::vector<IndexPair> SysMesh::all_edges() const noexcept
{
    std::vector<IndexPair> edges;
    edges.reserve(static_cast<std::size_t>(num_polys()) * 4);

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

int32_t SysMesh::num_edges() const noexcept
{
    std::vector<IndexPair> edges;
    edges.reserve(static_cast<std::size_t>(num_polys()) * 4);

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
    return static_cast<int32_t>(edges.size());
}

SysEdgePolys SysMesh::edge_polys(const IndexPair& edge) const noexcept
{
    SysEdgePolys   results = {};
    const SysVert& vert    = data->verts[edge.first];

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

    int32_t count = 0;
    for (int32_t pid : edge_polys(edge))
        if (poly_valid(pid) && poly_selected(pid))
            ++count;

    return count == 1;
}

// --------------------------------------------------------------------------
// Polygons
// --------------------------------------------------------------------------

const std::vector<int32_t>& SysMesh::all_polys() const noexcept
{
    return data->polys.valid_indices();
}

int32_t SysMesh::num_polys() const noexcept
{
    return data->polys.size();
}

int32_t SysMesh::poly_buffer_size() const noexcept
{
    return data->polys.slot_count();
}

int32_t SysMesh::create_poly(const SysPolyVerts& verts, uint32_t material_id) noexcept
{
    SysPoly new_poly{};
    new_poly.verts       = verts;
    new_poly.material_id = material_id;

    const int32_t poly_index = data->polys.insert(new_poly);

    // If a new slot was appended (not a reused hole), extend all map poly arrays.
    if (poly_index == data->polys.slot_count() - 1)
    {
        for (int32_t i = 0; i < data->mesh_maps.slot_count(); ++i)
        {
            if (data->mesh_maps.is_valid(i) && data->mesh_maps[i])
                data->mesh_maps[i]->polys.resize(
                    static_cast<std::size_t>(data->polys.slot_count()));
        }
    }

    assert(!data->polys[poly_index].removed);

    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoCreatePoly>();
        undo->poly.index = poly_index;
        undo->poly.data  = new_poly;
        data->history->insert(std::move(undo));
    }

    for (int32_t vert_index : verts)
        data->verts[vert_index].polys.insert_unique(poly_index);

    data->topology_counter->change();
    return poly_index;
}

int32_t SysMesh::clone_poly(int32_t poly_index, const SysPolyVerts& new_verts) noexcept
{
    int32_t new_poly = create_poly(new_verts, data->polys[poly_index].material_id);
    if (data->polys[poly_index].selected)
        select_poly(new_poly, true);
    return new_poly;
}

void SysMesh::remove_poly(int32_t poly_index) noexcept
{
    assert(!data->polys[poly_index].removed && "Polygon has already been removed!");

    select_poly(poly_index, false);

    for (IndexPair edge : poly_edges(poly_index))
        if (edge_polys(edge).size() == 1)
            select_edge(edge, false);

    for (int32_t j = 0; j < data->mesh_maps.slot_count(); ++j)
    {
        if (data->mesh_maps.is_valid(j) && data->mesh_maps[j] && map_poly_valid(j, poly_index))
            map_remove_poly(j, poly_index);
    }

    if (!data->history->is_busy())
    {
        auto undo        = std::make_unique<UndoRemovePoly>();
        undo->poly.index = poly_index;
        undo->poly.data  = data->polys[poly_index];
        data->history->insert(std::move(undo));
    }

    for (int32_t vert_index : data->polys[poly_index].verts)
        data->verts[vert_index].polys.erase_element(poly_index);

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
    const SysPolyVerts& pv = data->polys[poly_index].verts;

    for (int32_t i = 0; i < pv.size(); ++i)
    {
        const int32_t a = pv[i];
        const int32_t b = pv[(i + 1) % pv.size()];

        if ((a == edge.first && b == edge.second) ||
            (a == edge.second && b == edge.first))
            return true;
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
    for (int32_t prev = poly.verts.size() - 1, next = 0;
         next < poly.verts.size();
         prev = next++)
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
    for (int32_t prev = poly.verts.size() - 1, next = 0; next < poly.verts.size(); prev = next++)
    {
        const glm::vec3& prev_pos = data->verts[poly.verts[prev]].pos;
        const glm::vec3& next_pos = data->verts[poly.verts[next]].pos;
        norm.x += (prev_pos.y - next_pos.y) * (prev_pos.z + next_pos.z);
        norm.y += (prev_pos.z - next_pos.z) * (prev_pos.x + next_pos.x);
        norm.z += (prev_pos.x - next_pos.x) * (prev_pos.y + next_pos.y);
    }

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
        pos += vert_position(pv[i]);
    return pos / static_cast<float>(pv.size());
}

bool SysMesh::poly_valid(int32_t poly_index) const noexcept
{
    return poly_index >= 0 && data->polys.is_valid(poly_index) && !data->polys[poly_index].removed;
}

// --------------------------------------------------------------------------
// Maps
// --------------------------------------------------------------------------

int32_t SysMesh::map_create(int32_t id, int32_t type, int32_t dim) noexcept
{
    // Idempotent: if a map with this id already exists return its slot.
    const int32_t existing = map_find(id);
    if (existing != -1)
        return existing;

    auto new_map = std::make_shared<SysMeshMap>(id, type, dim);

    // Parallel poly array must match current mesh slot count.
    new_map->polys.resize(static_cast<std::size_t>(data->polys.slot_count()));

    const int32_t index = data->mesh_maps.insert(new_map);

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
    // Default maps are permanent fixtures — they can be cleared but never removed.
    if (id == MESH_MAP_NORMALS || id == MESH_MAP_UV0)
        return false;

    const int32_t map = map_find(id);
    if (map == -1)
        return false;

    if (!data->history->is_busy())
    {
        auto undo       = std::make_unique<UndoMapRemove>();
        undo->mesh_data = data.get();
        undo->mesh_map  = data->mesh_maps[map];
        undo->index     = map;
        data->history->insert(std::move(undo));

        if (data->mesh_maps[map])
            data->mesh_maps[map]->selection.clear();
    }
    else
    {
        data->mesh_maps[map].reset();
    }

    data->mesh_maps.remove(map);
    data->topology_counter->change();
    return true;
}

int32_t SysMesh::map_find(int32_t id) const noexcept
{
    for (int32_t i : data->mesh_maps.valid_indices())
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
           "Map polygon vert count must match mesh polygon!");

    data->mesh_maps[map]->polys[poly_index].verts = pv;

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

    SysMapVert new_vert{};
    std::memcpy(new_vert.vec, vec, static_cast<std::size_t>(mesh_map.dim) * sizeof(float));

    const int32_t index = mesh_map.verts.insert(new_vert);

    if (!data->history->is_busy())
    {
        auto undo = std::make_unique<UndoMapCreateVertex>();
        std::memcpy(undo->vert.data.vec,
                    mesh_map.verts[index].vec,
                    static_cast<std::size_t>(mesh_map.dim) * sizeof(float));
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
    assert(mesh_map.verts.is_valid(vert_index) && "Map vertex does not exist!");

    map_vert_select(map, vert_index, false);

    if (!data->history->is_busy())
    {
        auto undo = std::make_unique<UndoMapRemoveVertex>();
        std::memcpy(undo->vert.data.vec,
                    mesh_map.verts[vert_index].vec,
                    static_cast<std::size_t>(mesh_map.dim) * sizeof(float));
        undo->vert.index = vert_index;
        undo->vert.map   = map;
        data->history->insert(std::move(undo));
    }

    mesh_map.verts[vert_index].removed = true; // kept for undo snapshot compat
    mesh_map.verts.remove(vert_index);
    data->topology_counter->change();
}

bool SysMesh::map_poly_valid(int32_t map, int32_t poly_index) const noexcept
{
    if (map < 0 || map >= data->mesh_maps.slot_count())
        return false;

    if (!data->mesh_maps.is_valid(map))
        return false;

    const auto& mp = data->mesh_maps[map];
    if (!mp)
        return false;

    if (!poly_valid(poly_index))
        return false;

    if (poly_index < 0 || poly_index >= static_cast<int32_t>(mp->polys.size()))
        return false;

    return !mp->polys[poly_index].verts.empty();
}

void SysMesh::map_remove_poly(int32_t map, int32_t poly_index)
{
    for (int32_t vert_index : data->mesh_maps[map]->polys[poly_index].verts)
        map_vert_select(map, vert_index, false);

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
                    static_cast<std::size_t>(data->mesh_maps[map]->dim) * sizeof(float));
        undo->vert.index = vert_index;
        undo->vert.map   = map;
        data->history->insert(std::move(undo));
    }

    std::memcpy(data->mesh_maps[map]->verts[vert_index].vec,
                new_vec,
                static_cast<std::size_t>(data->mesh_maps[map]->dim) * sizeof(float));
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

int32_t SysMesh::map_buffer_size(int32_t map) const noexcept
{
    return data->mesh_maps[map]->verts.slot_count();
}

// --------------------------------------------------------------------------
// Selection
// --------------------------------------------------------------------------

bool SysMesh::select_vert(int32_t vert_index, bool select) noexcept
{
    if (data->verts[vert_index].selected != select)
    {
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
            auto it = std::find(data->vert_selection.begin(),
                                data->vert_selection.end(),
                                vert_index);
            if (it != data->vert_selection.end())
                data->vert_selection.erase(it);
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
        if (!data->history->is_busy())
        {
            auto undo = std::make_unique<UndoClearVertSel>();
            undo->sel = data->vert_selection;
            data->history->insert(std::move(undo));
        }

        for (int32_t vert_index : data->vert_selection)
            data->verts[vert_index].selected = false;

        data->vert_selection.clear();
        data->select_counter->change();
    }
}

bool SysMesh::select_poly(int poly_index, bool select) noexcept
{
    if (data->polys[poly_index].selected != select)
    {
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
            auto it = std::find(data->poly_selection.begin(),
                                data->poly_selection.end(),
                                poly_index);
            if (it != data->poly_selection.end())
                data->poly_selection.erase(it);
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
        if (!data->history->is_busy())
        {
            auto undo = std::make_unique<UndoClearPolySel>();
            undo->sel = data->poly_selection;
            data->history->insert(std::move(undo));
        }

        for (int32_t poly_index : data->poly_selection)
            data->polys[poly_index].selected = false;

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
            undo->edge   = edge;
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
    // Guard: skip if slot is a hole (can happen during remove_vert).
    if (!data->mesh_maps[map]->verts.is_valid(vert_index))
        return;

    if (data->mesh_maps[map]->verts[vert_index].selected != select)
    {
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
            std::erase_if(data->mesh_maps[map]->selection,
                          [vert_index](int32_t v) { return v == vert_index; });
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
    for (int32_t vert_index : data->mesh_maps[map]->selection)
        data->mesh_maps[map]->verts[vert_index].selected = false;
    data->mesh_maps[map]->selection.clear();
}

// --------------------------------------------------------------------------
// Change counters
// --------------------------------------------------------------------------

const SysCounterPtr& SysMesh::change_counter() const noexcept
{
    return data->change_counter;
}
const SysCounterPtr& SysMesh::topology_counter() const noexcept
{
    return data->topology_counter;
}
const SysCounterPtr& SysMesh::deform_counter() const noexcept
{
    return data->deform_counter;
}
const SysCounterPtr& SysMesh::select_counter() const noexcept
{
    return data->select_counter;
}

IndexPair SysMesh::sort_edge(const IndexPair& edge) noexcept
{
    return (edge.first < edge.second) ? edge : IndexPair{edge.second, edge.first};
}

// --------------------------------------------------------------------------
// Topology traversal
// --------------------------------------------------------------------------

namespace
{
    struct PairHash
    {
        size_t operator()(const IndexPair& p) const noexcept
        {
            const uint64_t a = static_cast<uint32_t>(p.first);
            const uint64_t b = static_cast<uint32_t>(p.second);
            return static_cast<size_t>((a << 32) ^ b);
        }
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

    auto face_adjacent_edge_at_vert = [&](int32_t p, const IndexPair& cur, int32_t v) -> IndexPair {
        if (!poly_valid(p))
            return IndexPair{-1, -1};

        const SysPolyVerts& pv = poly_verts(p);
        if (pv.size() != 4)
            return IndexPair{-1, -1};

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

        const int32_t a = pv[cur_i];
        const int32_t b = pv[(cur_i + 1) & 3];

        if (v != a && v != b)
            return IndexPair{-1, -1};

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

    auto next_edge_at_vertex = [&](const IndexPair& cur, int32_t at) -> IndexPair {
        if (!vert_valid(at))
            return IndexPair{-1, -1};

        const SysVertEdges inc = undirected_edges_at_vert(at);

        bool hasCur = false;
        for (const IndexPair& e : inc)
            if (e == cur)
                hasCur = true;
        if (!hasCur)
            return IndexPair{-1, -1};

        std::unordered_set<IndexPair, PairHash> turn = {};

        const SysEdgePolys polys = edge_polys(cur);
        if (polys.size() != 2)
            return IndexPair{-1, -1};

        for (int32_t p : polys)
        {
            const IndexPair adj = face_adjacent_edge_at_vert(p, cur, at);
            if (adj.first >= 0)
                turn.insert(adj);
        }

        IndexPair candidate{-1, -1};
        int32_t   count = 0;

        for (const IndexPair& e : inc)
        {
            if (e == cur || turn.contains(e))
                continue;
            candidate = e;
            if (++count > 1)
                break;
        }

        return (count == 1) ? candidate : IndexPair{-1, -1};
    };

    auto walk = [&](int32_t from, int32_t to) -> std::vector<IndexPair> {
        std::vector<IndexPair> out;
        out.reserve(64);

        std::unordered_set<IndexPair, PairHash> visited;
        IndexPair                               cur = sort_edge(IndexPair{from, to});
        int32_t                                 at  = to;

        for (;;)
        {
            if (!visited.insert(cur).second)
                break;
            out.push_back(cur);

            const IndexPair nxt = next_edge_at_vertex(cur, at);
            if (nxt.first < 0)
                break;

            at  = (nxt.first == at) ? nxt.second : nxt.first;
            cur = nxt;
        }

        return out;
    };

    const std::vector<IndexPair> fwd = walk(seed.first, seed.second);
    const std::vector<IndexPair> bwd = walk(seed.second, seed.first);

    if (fwd.empty() && bwd.empty())
        return {};

    std::vector<IndexPair> result;
    result.reserve(bwd.size() + fwd.size());

    for (int32_t i = static_cast<int32_t>(bwd.size()) - 1; i >= 0; --i)
        result.push_back(bwd[static_cast<std::size_t>(i)]);

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

    auto find_edge_index_in_quad = [&](int32_t p, const IndexPair& e, int32_t& outI) noexcept -> bool {
        if (!poly_is_quad(p))
            return false;

        const SysPolyVerts& pv = poly_verts(p);
        for (int32_t i = 0; i < 4; ++i)
        {
            const int32_t a = pv[i];
            const int32_t b = pv[(i + 1) & 3];
            if ((a == e.first && b == e.second) ||
                (a == e.second && b == e.first))
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
        return sort_edge(IndexPair{pv[i2], pv[(i2 + 1) & 3]});
    };

    auto walk = [&](int32_t startPoly, const IndexPair& startEdge) -> std::vector<IndexPair> {
        std::vector<IndexPair> out;
        out.reserve(64);

        std::unordered_set<IndexPair, PairHash> visited;
        visited.reserve(128);

        IndexPair curEdge = sort_edge(startEdge);
        int32_t   curPoly = startPoly;

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

            const SysEdgePolys oppPolys = edge_polys(opp);
            if (oppPolys.size() != 2)
                break;

            const int32_t nextPoly = (oppPolys[0] == curPoly) ? oppPolys[1] : oppPolys[0];
            if (!poly_is_quad(nextPoly))
                break;

            curEdge = opp;
            curPoly = nextPoly;
        }

        return out;
    };

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

    std::vector<IndexPair> aSide = walk(seedPoly, seed);
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
