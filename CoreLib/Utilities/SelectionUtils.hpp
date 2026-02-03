#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

#include "SysMesh.hpp"

class Scene;

/**
 * @file SelectionUtils.hpp
 * @brief Scene-wide selection helpers (resolution + conversion + aggregate queries).
 *
 * This module provides consistent rules for operating on the current selection across
 * Scene::activeMeshes().
 *
 * Core rule (per active mesh):
 *  - If the selection is empty in the current Scene::selectionMode(), fall back to "all"
 *    elements of that mode for that mesh.
 *
 * Common usage tips:
 *  - Transform tools (Move/Rotate/Scale) usually operate on vertices:
 *      - auto mv = sel::to_verts(scene);
 *  - Gizmo pivot (default): bounds center:
 *      - glm::vec3 pivot = sel::selection_center(scene);
 *  - Gizmo scaling / handle size:
 *      - float r = sel::selection_radius(scene);
 *      - float size = glm::max(r * 0.15f, kMinGizmoSize);
 *  - Alternate pivot: mean (centroid):
 *      - glm::vec3 pivot = sel::selection_center_mean(scene);
 *  - Selection box / gizmo sizing:
 *      - sel::Aabb b = sel::selection_bounds(scene);
 *  - "Surface" gizmo (place at representative polygon on selection):
 *      - glm::vec3 pos = {}, n = {};
 *      - bool anchored = sel::selection_surface_anchor(scene, pos, n);
 *
 * Notes:
 *  - This file contains queries/conversions only. No mutation, no rendering, no undo/redo.
 *  - All results are per mesh and only consider Scene::activeMeshes().
 */
namespace sel
{
    using MeshVertMap = std::unordered_map<SysMesh*, std::vector<int32_t>>;
    using MeshEdgeMap = std::unordered_map<SysMesh*, std::vector<IndexPair>>;
    using MeshPolyMap = std::unordered_map<SysMesh*, std::vector<int32_t>>;

    struct Aabb
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        bool      valid = false;
    };

    /**
     * @brief Convert current scene selection into per-mesh vertex indices.
     *
     * Rules (per mesh):
     *  - VERTS mode: selected_verts() else all_verts()
     *  - EDGES mode: endpoints of selected_edges() else endpoints of all_edges()
     *  - POLYS mode: verts of selected_polys() else verts of all_polys()
     *
     * Result is unique per mesh (no duplicate vertex indices).
     */
    [[nodiscard]] MeshVertMap to_verts(Scene* scene);

    /**
     * @brief Return per-mesh edges when in EDGES mode.
     *
     * Rules (per mesh):
     *  - EDGES mode: selected_edges() else all_edges()
     *  - Otherwise: empty map.
     */
    [[nodiscard]] MeshEdgeMap to_edges(Scene* scene);

    /**
     * @brief Return per-mesh polygons when in POLYS mode.
     *
     * Rules (per mesh):
     *  - POLYS mode: selected_polys() else all_polys()
     *  - Otherwise: empty map.
     */
    [[nodiscard]] MeshPolyMap to_polys(Scene* scene);

    /**
     * @brief True if there is any selection in the current Scene::selectionMode()
     * across Scene::activeMeshes().
     */
    [[nodiscard]] bool has_selection(Scene* scene) noexcept;

    /**
     * @brief Scene-wide bounds (AABB) of the current selection (or all, if selection empty in current mode).
     *
     * Uses sel::to_verts(scene), therefore respects SelectionMode and the empty-selection fallback.
     */
    [[nodiscard]] Aabb selection_bounds(Scene* scene) noexcept;

    /**
     * @brief Scene-wide selection center computed as the arithmetic mean of selected vertices (centroid).
     *
     * Uses sel::to_verts(scene), therefore respects SelectionMode and the empty-selection fallback.
     */
    [[nodiscard]] glm::vec3 selection_center_mean(Scene* scene) noexcept;

    /**
     * @brief Scene-wide selection center computed as the center of the selection AABB (bounds center).
     *
     * Uses sel::selection_bounds(scene).
     */
    [[nodiscard]] glm::vec3 selection_center_bounds(Scene* scene) noexcept;

    /**
     * @brief Default selection center used by tools/gizmos.
     *
     * Currently returns bounds center (see selection_center_bounds()).
     */
    [[nodiscard]] glm::vec3 selection_center(Scene* scene) noexcept;

    /**
     * @brief Approximate scene-wide "selection normal" suitable for surface-aligned gizmos.
     *
     * Heuristics:
     *  - POLYS mode: average polygon normals (selected, else all polys), normalized.
     *  - EDGES/VERTS mode: average normals of polygons touching the selected (or all) verts, normalized.
     *  - Fallback: +Z.
     */
    [[nodiscard]] glm::vec3 selection_normal(Scene* scene) noexcept;

    /**
     * @brief Compute a reasonable surface anchor (position + normal) for gizmo placement.
     *
     * v1 implementation (cheap):
     *  - Find a representative polygon whose center is closest to selection_center_bounds().
     *  - Return that polygon center + polygon normal.
     *  - If no polygons can be found, fall back to pivot + selection_normal().
     *
     * @return True if anchored to an actual polygon; false if fallback was used.
     */
    [[nodiscard]] bool selection_surface_anchor(Scene* scene, glm::vec3& outPos, glm::vec3& outN) noexcept;

    /**
     * @brief Scene-wide selection radius derived from the selection bounds.
     *
     * Defined as half the diagonal length of the selection AABB.
     * Commonly used for gizmo sizing and handle scaling.
     *
     * Returns 0 if the selection is empty or degenerate.
     */
    [[nodiscard]] float selection_radius(Scene* scene) noexcept;

} // namespace sel

namespace sel
{
    [[nodiscard]] MeshVertMap selected_verts(Scene* scene);
    [[nodiscard]] MeshEdgeMap selected_edges(Scene* scene);
    [[nodiscard]] MeshPolyMap selected_polys(Scene* scene);

    enum class EdgeDerivePolicy : uint8_t
    {
        PolyEdges   = 0, // all edges of selected polys
        OutlineOnly = 1  // only edges at selection boundary
    };

    [[nodiscard]] MeshEdgeMap connect_edges(Scene* scene, EdgeDerivePolicy policy = EdgeDerivePolicy::OutlineOnly);
} // namespace sel
