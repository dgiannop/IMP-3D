#pragma once

#include <EdgeSet.hpp>
#include <HoleList.hpp>
#include <SmallList.hpp>
#include <SysCounter.hpp>
#include <cstdint>
#include <glm/vec3.hpp>
#include <memory>
#include <vector>

#include "History.hpp"
#include "SysMesh.hpp"

// ------------------------------------------------------------------
// Default map slot indices — always present, never removed.
// Map 0: per-vertex normals (dim=3)
// Map 1: UV channel 0      (dim=2)
// ------------------------------------------------------------------
inline constexpr int32_t MESH_MAP_NORMALS = 0;
inline constexpr int32_t MESH_MAP_UV0     = 1;

// ------------------------------------------------------------------
// Vertex
// ------------------------------------------------------------------
struct SysVert
{
    SysVert() : removed(false), selected(false), modified(true) {}

    SysVertPolys polys;
    glm::vec3    pos;
    bool         removed;
    bool         selected;
    bool         modified;
};

// ------------------------------------------------------------------
// Polygon
// ------------------------------------------------------------------
struct SysPoly
{
    SysPoly() : material_id(0), removed(false), selected(false) {}

    SysPolyVerts verts;
    uint32_t     material_id;
    bool         removed;
    bool         selected;
};

struct SysFullPoly
{
    SysPoly data;
    int32_t index;
};

// ------------------------------------------------------------------
// Map vertex — now managed by HoleList, so 'removed' flag is no
// longer needed for free-list logic but kept for undo/redo snapshots.
// ------------------------------------------------------------------
struct SysMapVert
{
    SysMapVert() : removed(false), selected(false) {}

    float vec[4];
    bool  removed;
    bool  selected;
};

// ------------------------------------------------------------------
// Map polygon — indexed directly by mesh poly index (parallel array),
// NOT a HoleList. Resized to match data->polys.slot_count().
// ------------------------------------------------------------------
struct SysMapPoly
{
    SysPolyVerts verts;
};

struct SysFullMapVert
{
    SysFullMapVert() : map(-1), index(-1) {}

    SysMapVert data;
    int32_t    map;
    int32_t    index;
};

struct SysFullMapPoly
{
    SysFullMapPoly() : map(-1), index(-1) {}

    SysMapPoly data;
    int32_t    map;
    int32_t    index;
};

// ------------------------------------------------------------------
// Mesh map
// verts:  HoleList — stable indices, holes reused on insert
// polys:  plain vector — parallel array indexed by mesh poly slot
// selection: plain vector of selected vert indices
// ------------------------------------------------------------------
struct SysMeshMap
{
    SysMeshMap() : id(-1), type(-1), dim(0) {}
    SysMeshMap(int32_t id_, int32_t type_, int32_t dim_) : id(id_), type(type_), dim(dim_)
    {
    }

    int32_t id;
    int32_t type;
    int32_t dim;

    HoleList<SysMapVert>    verts;     ///< Stable-index map vertices
    std::vector<SysMapPoly> polys;     ///< Parallel to mesh poly slots
    std::vector<int32_t>    selection; ///< Selected vert indices
};

// ------------------------------------------------------------------
// Top-level mesh data
// ------------------------------------------------------------------
struct SysMeshData
{
    SysMeshData() : history{nullptr},
                    history_busy{false},
                    change_counter{std::make_shared<SysCounter>()},
                    topology_counter{std::make_shared<SysCounter>()},
                    deform_counter{std::make_shared<SysCounter>()},
                    select_counter{std::make_shared<SysCounter>()}
    {
        // Wire change counter as parent of all sub-counters.
        topology_counter->addParent(change_counter);
        deform_counter->addParent(change_counter);
        select_counter->addParent(change_counter);
    }

    /// Geometry
    HoleList<SysVert> verts;
    HoleList<SysPoly> polys;

    /// Maps — slot 0 = normals, slot 1 = UV0, always present (see SysMesh ctor)
    HoleList<std::shared_ptr<SysMeshMap>> mesh_maps;

    /// Selections
    EdgeSet                edge_selection_set;
    std::vector<int32_t>   vert_selection;
    std::vector<int32_t>   poly_selection;
    std::vector<IndexPair> edge_selection;

    /// History
    std::unique_ptr<History> history;
    bool                     history_busy;

    /// Change counters
    SysCounterPtr change_counter;
    SysCounterPtr topology_counter;
    SysCounterPtr deform_counter;
    SysCounterPtr select_counter;
};
