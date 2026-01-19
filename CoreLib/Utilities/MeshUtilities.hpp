#ifndef MESH_UTILITIES_HPP_INCLUDED
#define MESH_UTILITIES_HPP_INCLUDED

#include <glm/glm.hpp>
#include <vector>

class SysMesh;

/**
 * @file MeshUtilities.hpp
 * @brief Miscellaneous mesh utility functions and CPU-side render-data extraction.
 *
 * This header contains lightweight, free functions that operate on SysMesh
 * and helpers to extract CPU-side arrays used for rendering, selection
 * visualization, or GPU uploads.
 */

// ----------------------------------------------------------
// Editing utilities
// ----------------------------------------------------------

/**
 * @brief Center the mesh around the origin.
 *
 * Typically computes the mesh bounds or centroid and applies a translation
 * so the mesh is centered at (0,0,0). Implementation-defined whether it
 * uses centroid or bounding-box center.
 *
 * @param mesh Target mesh (must not be null)
 */
void centerMesh(SysMesh* mesh);

/**
 * @brief Uniformly scale the mesh.
 *
 * Applies a uniform scale to vertex positions.
 *
 * @param mesh Target mesh (must not be null)
 * @param amount Scale factor (1.0 leaves mesh unchanged)
 */
void scaleMesh(SysMesh* mesh, float amount = 1.f);

/**
 * @brief Validate or compute mesh normals.
 *
 * Implementation-defined: may verify winding consistency, check for
 * degenerate faces, and/or update derived normal data.
 *
 * @param mesh Target mesh (must not be null)
 */
void checkMeshNormals(SysMesh* mesh);

// ----------------------------------------------------------
// CPU-side render data extraction
// ----------------------------------------------------------

/**
 * @brief CPU-side triangle stream extracted from a mesh.
 *
 * Conventions:
 * - verts: 3 positions per triangle
 * - norms: 3 normals per triangle (often face-varying / per-corner)
 * - uvPos: 3 UVs per triangle (if available; may be empty)
 * - matIds: 1 material id per triangle (or 3 per triangle depending on implementation;
 *   this struct assumes 1 per triangle unless otherwise documented by implementation)
 */
struct MeshData
{
    /** @brief Triangle vertex positions (3 per triangle). */
    std::vector<glm::vec3> verts;

    /** @brief Triangle normals (typically 3 per triangle / per-corner). */
    std::vector<glm::vec3> norms;

    /** @brief Triangle UVs (typically 3 per triangle / per-corner). */
    std::vector<glm::vec2> uvPos;

    /** @brief Per-triangle material ids (typically 1 per triangle). */
    std::vector<uint32_t> matIds;
};

/**
 * @brief Extract coarse mesh triangles into a CPU-side stream.
 *
 * Produces triangle-expanded arrays suitable for direct upload into a
 * GPU vertex buffer or for CPU raster/debug rendering.
 *
 * @param mesh Source mesh
 * @return Extracted triangle stream
 */
[[nodiscard]] MeshData extractMeshData(const SysMesh* mesh);

/**
 * @brief Extract mesh edges as a line-list of positions.
 *
 * Output is a sequence of positions, where each pair represents one line segment:
 * [a0,b0, a1,b1, ...]
 *
 * @param mesh Source mesh
 * @return Line-list positions (2 per edge)
 */
[[nodiscard]] std::vector<glm::vec3> extractMeshEdges(const SysMesh* mesh);

/**
 * @brief Extract vertex positions preserving SysMesh slot indexing.
 *
 * The returned array is sized to the SysMesh vertex slot range; removed/invalid
 * vertices are represented by a default value (typically (0,0,0)) so that
 * indices can reference SysMesh vertex IDs directly.
 *
 * @param sys Source mesh
 * @return Slot-aligned vertex positions
 */
[[nodiscard]] std::vector<glm::vec3> extractMeshPositionsOnly(const SysMesh* sys); // TODO: rename to extractUniqueSlotPositions

/**
 * @brief Extract coarse triangle indices referencing SysMesh vertex slots.
 *
 * Indices reference the array returned by extractMeshPositionsOnly(), meaning
 * they index directly into SysMesh vertex slot IDs.
 *
 * @param sys Source mesh
 * @return Triangle indices (3 per triangle)
 */
[[nodiscard]] std::vector<uint32_t> extractMeshTriIndices(const SysMesh* sys);

/**
 * @brief Extract triangle-expanded positions only.
 *
 * Returns a triangle stream containing 3 positions per triangle.
 *
 * @param mesh Source mesh
 * @return Triangle-expanded positions
 */
[[nodiscard]] std::vector<glm::vec3> extractTriPositionsOnly(const SysMesh* mesh);

/**
 * @brief Extract per-polygon normals only.
 *
 * Note: name suggests polygon normals; output format is implementation-defined
 * (e.g., one normal per polygon, or triangle-expanded normals).
 *
 * @param mesh Source mesh
 * @return Polygon normal list
 */
[[nodiscard]] std::vector<glm::vec3> extractPolyNormasOnly(const SysMesh* mesh);

/**
 * @brief Extract mesh edges as an index list referencing SysMesh vertex slots.
 *
 * Output is a sequence of indices, where each pair represents one edge:
 * [a0,b0, a1,b1, ...]
 *
 * Indices typically reference SysMesh vertex IDs / slots.
 *
 * @param mesh Source mesh
 * @return Edge index list (2 per edge)
 */
[[nodiscard]] std::vector<uint32_t> extractMeshEdgeIndices(const SysMesh* mesh);

/**
 * @brief Extract selected vertex IDs.
 * @param sys Source mesh
 * @return List of selected vertex indices (SysMesh vertex IDs)
 */
[[nodiscard]] std::vector<uint32_t> extractSelectedVertices(const SysMesh* sys);

/**
 * @brief Extract selected edge endpoints as an index list.
 *
 * Output is [a0,b0, a1,b1, ...] where each pair forms one selected edge.
 *
 * @param sys Source mesh
 * @return Selected edge index list (2 per edge)
 */
[[nodiscard]] std::vector<uint32_t> extractSelectedEdges(const SysMesh* sys);

/**
 * @brief Extract selected polygon triangles as an index list.
 *
 * Produces triangle indices for selected polygons. Indexing and target
 * position buffer depends on implementation (commonly SysMesh slot indexing).
 *
 * @param sys Source mesh
 * @return Triangle index list (3 per triangle)
 */
[[nodiscard]] std::vector<uint32_t> extractSelectedPolyTriangles(const SysMesh* sys);

#endif
