#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <vector>

#include "History.hpp"
#include "SmallList.hpp"
#include "SysCounter.hpp"

using IndexPair    = std::pair<int32_t, int32_t>;
using SysVertPolys = SmallList<int32_t, 6>;
using SysEdgePolys = SmallList<int32_t, 2>;
using SysPolyVerts = SmallList<int32_t, 4>;
using SysPolyEdges = SmallList<IndexPair, 4>;
using SysVertEdges = SmallList<IndexPair, 6>;

class SysMesh
{
public:
    explicit SysMesh();
    SysMesh(const SysMesh&)            = delete;
    SysMesh& operator=(const SysMesh&) = delete;

    SysMesh(SysMesh&& other) noexcept;
    SysMesh& operator=(SysMesh&& other) noexcept;

    ~SysMesh();

    /// Empties the mesh, removing all geometry.
    /// Map slots 0 (normals) and 1 (UV0) are preserved but cleared.
    void clear();

    /// Reserve internal storage for the expected number of vertices.
    /// Derives polygon, edge, map, and selection buffer sizes automatically.
    /// Call before bulk creation (e.g. glTF import) to minimise reallocations.
    void reserve(int32_t vert_count) noexcept;

    History* history() const noexcept;

    /// Releases the current mesh history so that it can be inserted into a
    /// global history. Replaces the local history with a fresh one.
    std::unique_ptr<History> release_history();

    /// Vertices ------------------------------------------

    /// @return A list of all valid vertex indices in the mesh.
    [[nodiscard]] const std::vector<int32_t>& all_verts() const noexcept;

    /// @return The number of valid (non-removed) vertices.
    [[nodiscard]] int32_t num_verts() const noexcept;

    /// @return The size of the vertex buffer (total slot count including holes).
    [[nodiscard]] int32_t vert_buffer_size() const noexcept;

    /// @return Index of a newly created vertex at pos.
    int32_t create_vert(const glm::vec3& pos) noexcept;

    /// @return Index of a new vertex cloned from vert_index at pos.
    int32_t clone_vert(int32_t vert_index, const glm::vec3& pos) noexcept;

    void remove_vert(int32_t vert_index) noexcept;
    void move_vert(int32_t vert_index, const glm::vec3& new_pos) noexcept;

    [[nodiscard]] const glm::vec3&    vert_position(int32_t vert_index) const noexcept;
    [[nodiscard]] const SysVertPolys& vert_polys(int32_t vert_index) const noexcept;
    [[nodiscard]] SysVertEdges        vert_edges(int32_t vert_index) const noexcept;

    [[nodiscard]] bool boundary_vert(int32_t vert_index) const noexcept;
    [[nodiscard]] bool outline_vert(int32_t vert_index) const noexcept;
    [[nodiscard]] bool vert_valid(int32_t vert_index) const noexcept;

    /// Edges ---------------------------------------------

    [[nodiscard]] std::vector<IndexPair> all_edges() const noexcept;
    [[nodiscard]] int32_t                num_edges() const noexcept;
    [[nodiscard]] SysEdgePolys           edge_polys(const IndexPair& edge) const noexcept;
    [[nodiscard]] bool                   boundary_edge(const IndexPair& edge) const noexcept;
    [[nodiscard]] bool                   outline_edge(const IndexPair& edge) const noexcept;

    /// Polygons ------------------------------------------

    [[nodiscard]] const std::vector<int32_t>& all_polys() const noexcept;
    [[nodiscard]] int32_t                     num_polys() const noexcept;
    [[nodiscard]] int32_t                     poly_buffer_size() const noexcept;

    int32_t create_poly(const SysPolyVerts& verts, uint32_t material_id = 0) noexcept;
    int32_t clone_poly(int32_t poly_index, const SysPolyVerts& new_verts) noexcept;
    void    remove_poly(int32_t poly_index) noexcept;

    [[nodiscard]] uint32_t            poly_material(int32_t poly_index) const noexcept;
    void                              set_poly_material(int32_t  poly_index,
                                                        uint32_t material_id) noexcept;
    [[nodiscard]] bool                poly_has_edge(int32_t          poly_index,
                                                    const IndexPair& edge) const noexcept;
    [[nodiscard]] const SysPolyVerts& poly_verts(int32_t poly_index) const noexcept;
    [[nodiscard]] SysPolyEdges        poly_edges(int32_t poly_index) const noexcept;
    [[nodiscard]] glm::vec3           poly_normal(int32_t poly_index) const noexcept;
    [[nodiscard]] glm::vec3           poly_center(int32_t poly_index) const noexcept;
    [[nodiscard]] bool                poly_valid(int32_t poly_index) const noexcept;

    /// Maps ----------------------------------------------
    ///
    /// Two maps are always present and never removed:
    ///   MESH_MAP_NORMALS (0) — per-vertex normals, dim=3
    ///   MESH_MAP_UV0     (1) — UV channel 0,       dim=2
    ///
    /// Tools may rely on these indices without an existence check.

    int32_t map_create(int32_t id, int32_t type, int32_t dim) noexcept;
    bool    map_remove(int32_t id) noexcept;

    [[nodiscard]] int32_t map_find(int32_t id) const noexcept;
    [[nodiscard]] int32_t map_dim(int32_t map) const noexcept;

    void    map_create_poly(int32_t map, int32_t p, const SysPolyVerts& pv) noexcept;
    int32_t map_create_vert(int32_t map, const float* vec) noexcept;
    void    map_remove_vert(int32_t map, int32_t vert_index) noexcept;

    [[nodiscard]] bool map_poly_valid(int32_t map,
                                      int32_t poly_index) const noexcept;
    void               map_remove_poly(int32_t map,
                                       int32_t poly_index);
    void               map_vertex_move(int32_t      map,
                                       int32_t      vert_index,
                                       const float* new_vec) noexcept;

    [[nodiscard]] const SysPolyVerts& map_poly_verts(int32_t map,
                                                     int32_t poly_index) const;
    [[nodiscard]] const float*        map_vert_position(int32_t map,
                                                        int32_t vert_index) const noexcept;
    [[nodiscard]] int32_t             map_buffer_size(int32_t map) const noexcept;

    /// Selection -----------------------------------------

    bool                                      select_vert(int32_t vert_index, bool select) noexcept;
    [[nodiscard]] bool                        vert_selected(int32_t vert_index) const noexcept;
    [[nodiscard]] const std::vector<int32_t>& selected_verts() const noexcept;
    void                                      clear_selected_verts() noexcept;

    bool                                      select_poly(int poly_index, bool select) noexcept;
    [[nodiscard]] bool                        poly_selected(int32_t poly_index) const noexcept;
    [[nodiscard]] const std::vector<int32_t>& selected_polys() const noexcept;
    void                                      clear_selected_polys() noexcept;

    bool                                        select_edge(const IndexPair& edge,
                                                            bool             select) noexcept;
    [[nodiscard]] bool                          edge_selected(const IndexPair& edge) const noexcept;
    [[nodiscard]] const std::vector<IndexPair>& selected_edges() const noexcept;
    void                                        clear_selected_edges() noexcept;

    void                                      map_vert_select(int32_t map,
                                                              int32_t vert_index,
                                                              bool    select) noexcept;
    [[nodiscard]] bool                        map_vert_selected(int32_t map,
                                                                int32_t vert_index) const noexcept;
    [[nodiscard]] const std::vector<int32_t>& selected_map_verts(int32_t map) const noexcept;
    void                                      map_clear_selected_verts(int32_t map) noexcept;

    /// Change counters -----------------------------------

    [[nodiscard]] const SysCounterPtr& change_counter() const noexcept;
    [[nodiscard]] const SysCounterPtr& topology_counter() const noexcept;
    [[nodiscard]] const SysCounterPtr& deform_counter() const noexcept;
    [[nodiscard]] const SysCounterPtr& select_counter() const noexcept;

    /// Utilities -----------------------------------------

    /// Returns a sorted edge (lowest index first) for stable comparisons.
    static IndexPair sort_edge(const IndexPair& edge) noexcept;

    /// Topology traversal --------------------------------

    [[nodiscard]] std::vector<IndexPair> edge_loop(const IndexPair& seed) const;
    [[nodiscard]] std::vector<IndexPair> edge_ring(const IndexPair& seed) const;

private:
    std::shared_ptr<struct SysMeshData> data;
};
