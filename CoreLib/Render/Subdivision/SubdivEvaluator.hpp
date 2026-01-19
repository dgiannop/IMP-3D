// SubdivEvaluator.hpp
#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SdsMesh.hpp"
#include "SysMesh.hpp"

/**
 * @class SubdivEvaluator
 * @brief Evaluates a SysMesh through OpenSubdiv (via SdsMesh) and exposes refined buffers.
 *
 * Usage:
 *  - Topology changes: onTopologyChanged(mesh, level)
 *  - Level only:       onLevelChanged(level)
 *  - Deform only:      evaluate()
 *
 * UVs:
 *  - Uses SysMesh mapId=1 as FVar channel 0.
 *  - Face-varying values are keyed by SysMesh map-vertex IDs (NOT welded by float equality),
 *    preserving seams and islands correctly.
 *
 * Materials:
 *  - Treated as face-uniform and interpolated across levels using OSD.
 */
class SubdivEvaluator final
{
public:
    SubdivEvaluator()  = default;
    ~SubdivEvaluator() = default;

    SubdivEvaluator(const SubdivEvaluator&)            = delete;
    SubdivEvaluator& operator=(const SubdivEvaluator&) = delete;

    SubdivEvaluator(SubdivEvaluator&&) noexcept            = default;
    SubdivEvaluator& operator=(SubdivEvaluator&&) noexcept = default;

    void onTopologyChanged(SysMesh* mesh, int level);
    void onLevelChanged(int level);
    void evaluate(); // update refined vertex positions (and normals) at current level

    void recomputeNormalsFromTris();

    int currentLevel() const noexcept
    {
        return m_levelCurrent;
    }

    OpenSubdiv::Far::TopologyRefiner* refiner() const noexcept
    {
        return m_sdsMesh.valid() ? m_sdsMesh.refiner() : nullptr;
    }

    // --- Refined outputs (for current level) ---
    std::span<const glm::vec3> vertices() const noexcept
    {
        return m_verts;
    }

    std::span<const glm::vec3> normals() const noexcept
    {
        return m_norms;
    }

    const std::vector<glm::vec2>& uvs() const noexcept
    {
        return m_uvs;
    }

    const std::vector<uint32_t>& triangleUVIndices() const noexcept
    {
        return m_triUV;
    }

    std::span<const uint32_t> triangleIndices() const noexcept
    {
        return m_tris;
    }

    std::span<const uint32_t> triangleMaterialIds() const noexcept
    {
        return m_triMat;
    }

    std::span<const std::pair<uint32_t, uint32_t>> refinedEdges() const noexcept
    {
        return m_edges;
    }

    // --- Base -> limit helpers (selection propagation) ---
    int                 limitVert(int baseVertIndex) const;
    std::vector<int>    limitEdges(IndexPair baseEdge) const;

    // --- Face-uniform helpers (current level) ---

    /// @return Material ID for the given OSD face index at the current level.
    /// Returns 0 if unavailable/out of range.
    uint32_t faceMaterialId(int face) const noexcept;

    SdsMesh::IndexArray edge(int limitEdge) const noexcept
    {
        return m_sdsMesh.edge(limitEdge);
    }

    // --- Convenience passthroughs ---
    int numSubdivFaces() const
    {
        return m_sdsMesh.num_faces();
    }

    SdsMesh::IndexArray subdivFaceVerts(int face) const
    {
        return m_sdsMesh.face_verts(face);
    }

    SdsMesh::IndexArray subdivFaceFVars(int face, int channel = 0) const
    {
        return m_sdsMesh.face_fvars(face, channel);
    }

    // --- Optional utilities ---
    std::vector<std::pair<uint32_t, uint32_t>> primaryEdges() const;
    std::vector<std::pair<int, int>>           refinedOutlineEdgesForPolys(const std::vector<int>& basePolys) const;
    std::vector<uint32_t>                      triangleIndicesForBasePoly(int basePoly) const;

private:
    void buildDescriptorFromMesh(SysMesh* mesh);
    void ensureRefinedTo(int level);
    void rebuildPerLevelProducts(int level);
    void sliceUVsForLevel(int level);

private:
    SysMesh* m_sysMesh = nullptr; // non-owning

    SdsMesh m_sdsMesh = {};

    int m_levelCurrent = 0;

    // --- Dense remaps (verts, polys) ---
    std::vector<int>             m_vremap;    // dense vert -> base vert
    std::unordered_map<int, int> m_vremapInv; // base vert -> dense vert
    std::vector<int>             m_premap;    // dense poly -> base poly
    std::unordered_map<int, int> m_premapInv; // base poly -> dense poly

    // --- Dense remaps (map verts -> fvar values) for mapId=1 ---
    std::vector<int>             m_tremap;    // dense fvar -> base map vert
    std::unordered_map<int, int> m_tremapInv; // base map vert -> dense fvar

    // --- Descriptor backing storage (alive across lifetime) ---
    std::vector<int> m_numVertsPerFace;
    std::vector<int> m_vertIndicesPerCorner; // dense vertex per coarse corner
    std::vector<int> m_fvarIndicesPerCorner; // dense fvar per coarse corner

    // Keep TopologyDescriptor fvar channel storage alive across create()
    OpenSubdiv::Far::TopologyDescriptor::FVarChannel m_uvChannel = {};

    // Level-0 fvar values (dense fvar indexing)
    std::vector<glm::vec2> m_fvarValuesL0;

    // All-level interpolated arrays (contiguous across levels)
    std::vector<int>       m_faceUniformAll; // materials, size = total faces
    std::vector<glm::vec2> m_fvarAll;        // UVs,       size = total fvars

    // --- Current level outputs ---
    std::vector<glm::vec3>                     m_verts;
    std::vector<glm::vec3>                     m_norms;
    std::vector<uint32_t>                      m_tris;
    std::vector<uint32_t>                      m_triUV;
    std::vector<uint32_t>                      m_triMat;
    std::vector<std::pair<uint32_t, uint32_t>> m_edges;

    // Current level UV values (level-local fvar indexing)
    std::vector<glm::vec2> m_uvs;
};
