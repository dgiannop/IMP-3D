// HeMeshBridge.hpp
#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <vector>

#include "HeMesh.hpp"  // required because HeExtractionResult stores HeMesh by value
#include "SysMesh.hpp" // needed for inline template wrapper (SysMesh type + selection access)

class SysMesh;

/**
 * @brief Options controlling SysMesh -> HeMesh extraction and commit application.
 *
 * Conventions:
 *  - SysMesh "normal map" is identified by map ID (default 0), resolved via SysMesh::map_find().
 *  - SysMesh "uv map"     is identified by map ID (default 1), resolved via SysMesh::map_find().
 *
 * IMPORTANT:
 *  SysMesh map APIs (map_poly_valid/map_poly_verts/map_vert_position/...) expect a MAP INDEX,
 *  not the map ID. We always call map_find(opt.*MapId) before accessing map polygons/verts.
 */
struct HeExtractionOptions
{
    // Region:
    //  editable = selected polys (or user-provided list)
    //  support  = neighbor polys across the editable *boundary* only
    bool includeBoundaryNeighbors = true;

    // Face-varying import
    bool importNormals = true; // Sys map ID 0 by convention
    bool importUVs     = true; // Sys map ID 1 by convention

    // SysMesh convention (MAP IDs, not map indices!)
    int32_t normalMapId = 0;
    int32_t uvMapId     = 1;
};

/**
 * @brief Result of extracting a region from SysMesh into a temporary tool HeMesh.
 *
 * Contains:
 *  - A HeMesh holding the region (editable + optional support ring)
 *  - Lists of Sys polys in editable and full region
 *  - ID mappings between Sys <-> He for verts and polys
 *  - A per-He poly flag (editable or support) for the ORIGINAL extracted region
 */
struct HeExtractionResult
{
    HeMesh mesh{};

    // Sanitized (valid + unique + sorted)
    std::vector<int32_t> editableSysPolys{};

    // Sorted + unique
    std::vector<int32_t> regionSysPolys{};

    // size = sys->vert_buffer_size(), -1 if not in region
    std::vector<int32_t> sysVertToHeVert{};

    // size = sys->poly_buffer_size(), -1 if not in region
    std::vector<int32_t> sysPolyToHePoly{};

    // size = mesh.vertCount(), -1 if new
    std::vector<int32_t> heVertToSysVert{};

    // size = mesh.polyCount(), -1 if new
    std::vector<int32_t> hePolyToSysPoly{};

    // size = mesh.polyCount(), 0/1 (ONLY valid for original extracted polys)
    std::vector<uint8_t> hePolyEditable{};
};

/**
 * @brief A minimal “diff” describing how to mutate SysMesh to match a final HeMesh.
 *
 * Intended usage:
 *  - build_commit_replace_editable() computes this from (extract, finalHe)
 *  - apply_commit() applies it to SysMesh with normal SysMesh undo recording
 */
struct HeMeshCommit
{
    // polys to remove in SysMesh (typically the editable polys), sorted high->low
    std::vector<int32_t> removePolys{};

    struct MoveVert
    {
        int32_t   sysVert{-1};
        glm::vec3 newPos{};
    };
    std::vector<MoveVert> moveVerts{};

    struct CreateVert
    {
        int32_t   heVert{-1};
        glm::vec3 pos{};
    };
    std::vector<CreateVert> createVerts{};

    struct CreatePoly
    {
        int32_t              hePoly{-1};
        std::vector<int32_t> heVerts{}; // He vert ids in winding order (resolved to Sys at apply)
        uint32_t             materialId{0};

        // Face-varying attributes per corner (aligned with heVerts / loops)
        bool                   hasNormals{false};
        bool                   hasUVs{false};
        std::vector<glm::vec3> normals{}; // per-corner
        std::vector<glm::vec2> uvs{};     // per-corner

        bool selectAfterCreate{false};
    };
    std::vector<CreatePoly> createPolys{};
};

// -----------------------------------------------------------------------------
// Extraction
// -----------------------------------------------------------------------------

/**
 * @brief Extract selected polys (editable) and optional boundary neighbors (support) into HeMesh.
 */
HeExtractionResult extract_selected_polys_to_hemesh(SysMesh*                   sys,
                                                    const HeExtractionOptions& opt);

/**
 * @brief Extract the provided editable polys and optional boundary neighbors into HeMesh.
 *
 * @param sys              Source mesh
 * @param editableSysPolys Polys considered editable by the tool
 * @param opt              Extraction options
 */
HeExtractionResult extract_polys_to_hemesh(SysMesh*                   sys,
                                           std::span<const int32_t>   editableSysPolys,
                                           const HeExtractionOptions& opt);

// -----------------------------------------------------------------------------
// Commit building / application
// -----------------------------------------------------------------------------

/**
 * @brief Build a commit that replaces the editable region in SysMesh with the finalHe topology.
 *
 * Rules:
 *  - All editable Sys polys are removed
 *  - Support polys are preserved unless finalHe changed them (we do not do that today)
 *  - New polys and editable polys in finalHe are recreated in SysMesh
 *  - Verts are moved if they correspond to existing Sys verts, otherwise created
 *
 * Robustness notes:
 *  - "Editable" determination is done from Sys-space editable set (not stale hePolyEditable),
 *    so topology-changing ops that delete/recreate He polys still behave correctly.
 */
HeMeshCommit build_commit_replace_editable(SysMesh*                   sys,
                                           const HeExtractionResult&  extract,
                                           const HeMesh&              finalHe,
                                           const HeExtractionOptions& opt);

// test
HeMeshCommit build_commit_replace_region(SysMesh*                   sys,
                                         const HeExtractionResult&  extract,
                                         const HeMesh&              finalHe,
                                         const HeExtractionOptions& opt);
/**
 * @brief Apply a previously built commit to SysMesh.
 *
 * This mutates SysMesh using its own APIs (create/remove/move/map_create_*),
 * so undo/redo is automatically recorded by SysMesh.
 */
void apply_commit(SysMesh*                   sys,
                  const HeExtractionResult&  extract,
                  const HeMeshCommit&        commit,
                  const HeExtractionOptions& opt);

// -----------------------------------------------------------------------------
// Convenience wrapper (template, header-only)
// -----------------------------------------------------------------------------

namespace he
{
    template<class Fn>
    void apply_selected(SysMesh* sys, const HeExtractionOptions& opt, Fn&& op)
    {
        if (!sys)
            return;

        HeExtractionResult extract = extract_selected_polys_to_hemesh(sys, opt);

        // Nothing editable => nothing to replace in SysMesh.
        if (extract.editableSysPolys.empty())
            return;

        // Let the tool mutate the extracted region in HeMesh-space.
        op(extract.mesh, extract);

        // Replace ONLY the editable Sys polys with the current HeMesh result.
        const HeMeshCommit commit =
            build_commit_replace_editable(sys, extract, extract.mesh, opt);

        apply_commit(sys, extract, commit, opt);
    }

} // namespace he
