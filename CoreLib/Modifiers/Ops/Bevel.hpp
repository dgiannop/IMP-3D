#pragma once

#include <cstdint>
#include <span>
#include <utility>

class SysMesh;

using IndexPair = std::pair<int32_t, int32_t>; // SysMesh vertex indices

// ============================================================================
// SysMesh-based bevel (fast, direct, geometry-first)
// ============================================================================
namespace ops::sys
{
    /**
     * @brief Bevel selected edges directly on SysMesh.
     *
     * This is the low-level, allocation-light implementation.
     * Produces correct topology suitable for loop selection.
     */
    void bevelEdges(SysMesh*                   mesh,
                    std::span<const IndexPair> edges,
                    float                      width);

    /**
     * @brief Bevel selected polygons.
     *
     * Semantics:
     *  - group == true  : bevel the outer boundary of the poly region
     *  - group == false : bevel each polygon independently
     *
     * Internally implemented as boundary-edge bevel.
     */
    void bevelPolys(SysMesh*                 mesh,
                    std::span<const int32_t> polys,
                    float                    width,
                    bool                     group);

    /**
     * @brief Bevel selected vertices.
     *
     * Creates new vertices on each incident edge and rebuilds the incident
     * faces. Caps are created only for closed vertex fans.
     */
    void bevelVerts(SysMesh*                 mesh,
                    std::span<const int32_t> verts,
                    float                    width);

} // namespace ops::sys

// ============================================================================
// HeMesh-based bevel (robust, high-level, slower)
// ============================================================================
namespace ops::he
{
    /**
     * @brief Bevel selected edges using HeMesh extraction + commit.
     *
     * Notes:
     *  - Extracts editable polys around the edges.
     *  - Performs bevel in HeMesh space.
     *  - Commits back by replacing only affected polys.
     *  - Robust for complex topology and map propagation.
     */
    void bevelEdges(SysMesh*                   mesh,
                    std::span<const IndexPair> edges,
                    float                      width);

} // namespace ops::he
