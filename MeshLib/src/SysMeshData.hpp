#ifndef SYS_MESH_DATA_HPP_INCLUDED
#define SYS_MESH_DATA_HPP_INCLUDED

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

struct SysVert
{
    SysVert() : removed(false), selected(false), modified(true)
    {
    }
    SysVertPolys polys;
    glm::vec3    pos;
    bool         removed;
    bool         selected;
    bool         modified;
};

struct SysPoly
{
    SysPoly() : material_id(0), removed(false), selected(false)
    {
    }
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

struct SysMapVert
{
    SysMapVert() : removed(false), selected(false)
    {
    }
    float vec[4];
    bool  removed;
    bool  selected;
};

struct SysMapPoly
{
    SysPolyVerts verts;
};

struct SysFullMapVert
{
    SysFullMapVert() : map(-1), index(-1)
    {
    }
    SysMapVert data;
    int32_t    map;
    int32_t    index;
};

struct SysFullMapPoly
{
    SysFullMapPoly() : map(-1), index(-1)
    {
    }
    SysMapPoly data;
    int32_t    map;
    int32_t    index;
};

struct SysMeshMap
{
    SysMeshMap() : id(-1), type(-1), dim(0)
    {
    }
    int32_t                 id;
    int32_t                 type;
    int32_t                 dim;
    std::vector<SysMapVert> verts;
    std::vector<int32_t>    free_verts;
    std::vector<SysMapPoly> polys;
    std::vector<int32_t>    selection;
};

struct SysMeshData
{
    SysMeshData() :
        history{nullptr},
        history_busy{false},
        change_counter{std::make_shared<SysCounter>()},
        topology_counter{std::make_shared<SysCounter>()},
        deform_counter{std::make_shared<SysCounter>()},
        select_counter{std::make_shared<SysCounter>()}
    {
        // Set the general change counter as the parent.
        topology_counter->addParent(change_counter);
        deform_counter->addParent(change_counter);
        select_counter->addParent(change_counter);
    }

    /// List of verts, polys and maps
    HoleList<SysVert>                     verts;
    HoleList<SysPoly>                     polys;
    HoleList<std::shared_ptr<SysMeshMap>> mesh_maps;

    /// Selections
    EdgeSet                edge_selection_set;
    std::vector<int32_t>   vert_selection;
    std::vector<int32_t>   poly_selection;
    std::vector<IndexPair> edge_selection;

    /// History
    std::unique_ptr<History> history;
    bool     history_busy;

    /// Change counters
    SysCounterPtr change_counter;
    SysCounterPtr topology_counter;
    SysCounterPtr deform_counter;
    SysCounterPtr select_counter;

    /// Edge cache and change counter value
    // std::vector<IndexPair> edges;
    // uint64_t edge_version = 0;
};

#endif
