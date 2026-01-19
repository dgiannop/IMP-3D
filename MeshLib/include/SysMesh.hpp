//
//  SysMesh.hpp
//  Mesh
//
//  Created by Daniel Giannopoulos on 6/20/15.
//  Copyright © 2015 Daniel Giannopoulos. All rights reserved.
//

#ifndef SYS_MESH_HPP_INCLUDED
#define SYS_MESH_HPP_INCLUDED

#include <glm/vec3.hpp>
#include <memory>
#include <vector>

#include "History.hpp"
#include "SmallList.hpp"
#include "SysCounter.hpp"

using IndexPair = std::pair<int32_t, int32_t>;
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
    void clear();

    History* history() const noexcept;

    /// Releases the current mesh history so that it can be inserted into a global
    /// history. This will clear out the mesh's local history so that it can begin
    /// recording new changes.
    std::unique_ptr<History> release_history();

    /// Vertices ------------------------------------------

    /// @return A list of all valid vertex indices in the mesh.
    [[nodiscard]] const std::vector<int32_t>& all_verts() const noexcept;

    /// @return The number of valid vertices in the mesh.
    [[nodiscard]] uint32_t num_verts() const noexcept;

    /// @return The size of the vertex buffer (the index range).
    [[nodiscard]] uint32_t vert_buffer_size() const noexcept;

    /// @return An index to a newly created vertex with the specified position.
    int32_t create_vert(const glm::vec3& pos) noexcept;

    /// @return A new vertex at the specified position.
    int32_t clone_vert(int32_t vert_index, const glm::vec3& pos) noexcept;

    /// Removes the specified vertex.
    void remove_vert(int32_t vert_index) noexcept;

    /// Moves the vertex to the specified position.
    void move_vert(int32_t vert_index, const glm::vec3& new_pos) noexcept;

    /// @return The position of the specified vertex.
    [[nodiscard]] const glm::vec3& vert_position(int32_t vert_index) const noexcept;

    /// @return The polygons connected to the specified vertex.
    [[nodiscard]] const SysVertPolys& vert_polys(int32_t vert_index) const noexcept;

    /// @return The edges connected to the specified vertex.
    [[nodiscard]] SysVertEdges vert_edges(int32_t vert_index) const noexcept;

    /// @return True if the vertex lies on a boundary edge.
    [[nodiscard]] bool boundary_vert(int32_t vert_index) const noexcept;

    /// @return True if the vertex lies on a boundary of selection.
    [[nodiscard]] bool outline_vert(int32_t vert_index) const noexcept;

    /// Edges ---------------------------------------------

    /// @return A list of all valid edges in the mesh (unique).
    std::vector<IndexPair> all_edges() const noexcept;

    /// @return The number of valid edges in the mesh.
    uint32_t num_edges() const noexcept;

    /// @return The polygons connected to the specified edge.
    SysEdgePolys edge_polys(const IndexPair& edge) const noexcept;

    /// @return True if the edge is a 1-manifold boundary edge.
    bool boundary_edge(const IndexPair& edge) const noexcept;

    /// @return True if edge is at outline of selection
    bool outline_edge(const IndexPair& edge) const noexcept;

    /// Polygons ------------------------------------------

    /// @return A list of all valid polygon indices in the mesh.
    [[nodiscard]] const std::vector<int32_t>& all_polys() const noexcept;

    /// @return The number of valid polygons in the mesh.
    [[nodiscard]] uint32_t num_polys() const noexcept;

    /// @return The size of the polygon buffer (the index range).
    [[nodiscard]] uint32_t poly_buffer_size() const noexcept;

    /// @return An index to a newly created polygon with the specified vertices and material.
    int32_t create_poly(const SysPolyVerts& verts, uint32_t material_id = 0) noexcept;

    /// @return An index to a newly created polygon from the one specified with the same
    /// properties but a different set of vertices.
    int32_t clone_poly(int32_t poly_index, const SysPolyVerts& new_verts) noexcept;

    /// Removes the specified polygon.
    void remove_poly(int32_t poly_index) noexcept;

    /// @return The material associated with the specified polygon.
    [[nodiscard]] uint32_t poly_material(int32_t poly_index) const noexcept;

    /// Changes the material associated with the specified polygon.
    void set_poly_material(int32_t poly_index, uint32_t material_id) noexcept;

    /// @return True if the polygon has the specified edge.
    [[nodiscard]] bool poly_has_edge(int32_t poly_index, const IndexPair& edge) const noexcept;

    /// @return The vertices of the specified polygon.
    [[nodiscard]] const SysPolyVerts& poly_verts(int poly_index) const noexcept;

    /// @return The edges of the specified polygon.
    /// note it returns the edge indices. Vert ordering might be wrong
    [[nodiscard]] SysPolyEdges poly_edges(int32_t poly_index) const noexcept;

    /// @return The normal of the polygon.
    [[nodiscard]] glm::vec3 poly_normal(int32_t poly_index) const noexcept;

    /// @return The center of the polygon.
    [[nodiscard]] glm::vec3 poly_center(int32_t poly_index) const noexcept;

    /// Maps ----------------------------------------------

    /// @return A new map with the specified ID, type, dimensions.
    int32_t map_create(int32_t id, int32_t type, int32_t dim) noexcept;

    /// Removes the map with the specified ID.
    bool map_remove(int32_t id) noexcept;

    /// @return The index of a map with the specified ID, or -1 if no such map
    /// is found.
    [[nodiscard]] int32_t map_find(int32_t id) const noexcept;

    /// @return The dimensions of the vectors in the specified map.
    [[nodiscard]] int32_t map_dim(int32_t map) const noexcept;

    /// Creates (maps) a new polygon with the specified vertices.
    void map_create_poly(int32_t map, int32_t p, const SysPolyVerts& pv) noexcept;

    /// Creates a vertex entry in the map.
    int32_t map_create_vert(int32_t map, const float* vec) noexcept;

    /// Removes a vertex entry in the map.
    void map_remove_vert(int32_t map, int32_t vert_index) noexcept;

    /// Returns true if the mesh map has the specified polygon mapped.
    [[nodiscard]] bool map_poly_valid(int32_t map, int32_t poly_index) const noexcept;

    /// Removes the specified polygon from the map.
    void map_remove_poly(int32_t map, int32_t poly_index);

    /// Moves the nth vertex entry in the map to the newly specified position.
    void map_vertex_move(int32_t map, int32_t vert_index, const float* new_vec) noexcept;

    /// @return The vertices of the specified mapped polygon.
    [[nodiscard]] const SysPolyVerts& map_poly_verts(int32_t map, int32_t poly_index) const;

    /// @return The position of the specified mapped vertex.
    const float* map_vert_position(int32_t map, int32_t vert_index) const noexcept;

    /// @return The size of the specified map buffer (the index range).
    [[nodiscard]] uint32_t map_buffer_size(int32_t map) const noexcept;

    /// Selection -----------------------------------------

    /// Selects/deselects the specified vertex.
    bool select_vert(int32_t vert_index, bool select) noexcept;

    /// @return True if vertex is selected.
    [[nodiscard]] bool vert_selected(int32_t vert_index) const noexcept;

    /// @return A list of all the selected vertex indices.
    const std::vector<int32_t>& selected_verts() const noexcept;

    /// Clear all selected vertices.
    void clear_selected_verts() noexcept;

    /// Selects/deselects the specified polygon.
    bool select_poly(int poly_index, bool select) noexcept;

    /// @return True if polygon is selected.
    [[nodiscard]] bool poly_selected(int32_t poly_index) const noexcept;

    /// @return A list of all the selected polygon indices.
    [[nodiscard]] const std::vector<int32_t>& selected_polys() const noexcept;

    /// Clear all selected polygons.
    void clear_selected_polys() noexcept;

    /// @return True if edge got selected, else false.
    bool select_edge(const IndexPair& edge, bool select) noexcept;

    /// @return True if edge is selected
    bool edge_selected(const IndexPair& edge) const noexcept;

    /// @return A list of all the selected edges.
    [[nodiscard]] const std::vector<IndexPair>& selected_edges() const noexcept;

    /// Clear all selected edges.
    void clear_selected_edges() noexcept;

    /// Select the Nth mapped vertex
    void map_vert_select(int32_t map, int32_t vert_index, bool select) noexcept;

    /// @return True if the Nth mapped vertex is selected
    [[nodiscard]] bool map_vert_selected(int32_t map, int32_t vert_index) const noexcept;

    /// @return A list of all the selected map vertex indices.
    [[nodiscard]] const std::vector<int32_t>& selected_map_verts(int32_t map) const noexcept;

    /// Clear all selected vertex from a map.
    void map_clear_selected_verts(int32_t map) noexcept;

    /// Change Counters -----------------------------------

    /// Provides a change counter which is modified whenever
    /// the mesh has been modified in any way.
    [[nodiscard]] const SysCounterPtr& change_counter() const noexcept;
    /// Provides a change counter which is modified whenever
    /// the mesh topology has been modified.
    [[nodiscard]] const SysCounterPtr& topology_counter() const noexcept;
    /// Provides a change counter which is modified whenever
    /// the mesh has been deformed
    [[nodiscard]] const SysCounterPtr& deform_counter() const noexcept;
    /// Provides a change counter which is modified whenever
    /// the mesh element selection has been modified.
    [[nodiscard]] const SysCounterPtr& select_counter() const noexcept;

    /// Returns a sorted edge (lowest index first) for stable edge comparisons
    static IndexPair sort_edge(const IndexPair& edge) noexcept;

    [[nodiscard]] bool poly_valid(int32_t poly_index) const noexcept;

    [[nodiscard]] bool vert_valid(int32_t vert_index) const noexcept;

    /// Topology Traversal -------------------------------

    /// @return Edge loop through the seed edge (follows connected edges through vertices).
    [[nodiscard]] std::vector<IndexPair> edge_loop(const IndexPair& seed) const;

    /// @return Edge ring through the seed edge (opposite-edge across quads).
    [[nodiscard]] std::vector<IndexPair> edge_ring(const IndexPair& seed) const;

private:
    std::shared_ptr<struct SysMeshData> data;
};

#endif
