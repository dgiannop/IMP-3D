// ============================================================
// Helpers/RenderGeometry.hpp
// ============================================================
#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.h>

#include "CoreTypes.hpp"

/**
 * @file RenderGeometry.hpp
 * @brief Small POD structs + selectors for choosing coarse vs subdiv GPU geometry.
 *
 * These helpers exist to keep Renderer.cpp smaller and to centralize the
 * "coarse vs subdiv" selection logic in one place.
 */

class SceneMesh;
class MeshGpuResources;

namespace render::geom
{
    // ============================================================
    // Filled triangles (SOLID / SHADED / depth-only)
    // ============================================================

    /**
     * @brief GPU geometry used for filled triangle rasterization (SOLID / SHADED / depth-only).
     * This represents a fully expanded triangle stream suitable for direct vkCmdDraw().
     * Buffers are NOT topology-stable and do NOT map 1:1 to SysMesh indices.
     *
     * Notes:
     * - UVs are OPTIONAL. If the mesh has no UVs, uvBuffer may be VK_NULL_HANDLE.
     * - Materials are currently REQUIRED (matBuffer must be valid) because the renderer
     *   uses per-triangle material IDs for shading decisions.
     */
    struct GfxMeshGeometry
    {
        VkBuffer posBuffer = VK_NULL_HANDLE; ///< vec3/vec4 position stream
        VkBuffer nrmBuffer = VK_NULL_HANDLE; ///< face-varying normals
        VkBuffer uvBuffer  = VK_NULL_HANDLE; ///< face-varying UVs (optional)
        VkBuffer matBuffer = VK_NULL_HANDLE; ///< per-triangle material IDs

        uint32_t vertexCount = 0; ///< number of vertices to draw (vkCmdDraw)

        [[nodiscard]] bool valid() const noexcept
        {
            return posBuffer != VK_NULL_HANDLE &&
                   nrmBuffer != VK_NULL_HANDLE &&
                   matBuffer != VK_NULL_HANDLE &&
                   vertexCount > 0;
        }

        [[nodiscard]] bool hasUvs() const noexcept
        {
            return uvBuffer != VK_NULL_HANDLE;
        }
    };

    // ============================================================
    // Ray tracing geometry (BLAS build + shader streams)
    // ============================================================

    /**
     * @brief GPU geometry used for ray tracing (BLAS build + optional shader shading streams).
     * - "build*" buffers are used for BLAS build inputs (shared positions + tight uint indices).
     * - "shade*" buffers + "shaderIndexBuffer" are used by the RT shaders (expanded streams).
     */
    struct RtMeshGeometry
    {
        VkBuffer buildPosBuffer = VK_NULL_HANDLE;
        uint32_t buildPosCount  = 0;

        VkBuffer buildIndexBuffer = VK_NULL_HANDLE;
        uint32_t buildIndexCount  = 0;

        VkBuffer shadePosBuffer = VK_NULL_HANDLE;
        uint32_t shadePosCount  = 0;

        VkBuffer shadeNrmBuffer = VK_NULL_HANDLE;
        uint32_t shadeNrmCount  = 0;

        VkBuffer shadeUvBuffer = VK_NULL_HANDLE;
        uint32_t shadeUvCount  = 0;

        VkBuffer shaderIndexBuffer = VK_NULL_HANDLE;
        uint32_t shaderTriCount    = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return buildPosBuffer != VK_NULL_HANDLE && buildPosCount > 0 &&
                   buildIndexBuffer != VK_NULL_HANDLE && buildIndexCount > 0;
        }

        [[nodiscard]] bool shaderValid() const noexcept
        {
            return shadePosBuffer != VK_NULL_HANDLE && shadePosCount > 0 &&
                   shaderIndexBuffer != VK_NULL_HANDLE && shaderTriCount > 0 &&
                   shadeNrmBuffer != VK_NULL_HANDLE && shadeNrmCount > 0 &&
                   shadeUvBuffer != VK_NULL_HANDLE && shadeUvCount > 0;
        }
    };

    [[nodiscard]] GfxMeshGeometry selectGfxGeometry(SceneMesh* sm, MeshGpuResources* gpu) noexcept;
    [[nodiscard]] RtMeshGeometry  selectRtGeometry(SceneMesh* sm) noexcept;

    // ============================================================
    // Selection overlay geometry (index buffers + pipelines)
    // ============================================================

    /**
     * @brief Pipelines needed to draw selection overlays for each selection mode.
     * Pass these in from Renderer (keeps this helper independent of Renderer members).
     */
    struct SelPipelines
    {
        VkPipeline vertVis = VK_NULL_HANDLE;
        VkPipeline vertHid = VK_NULL_HANDLE;

        VkPipeline edgeVis = VK_NULL_HANDLE;
        VkPipeline edgeHid = VK_NULL_HANDLE;

        VkPipeline polyVis = VK_NULL_HANDLE;
        VkPipeline polyHid = VK_NULL_HANDLE;
    };

    /**
     * @brief Selection draw inputs (VB + IB + pipelines).
     */
    struct SelDrawGeo
    {
        VkBuffer   posVb    = VK_NULL_HANDLE;
        VkBuffer   selIb    = VK_NULL_HANDLE;
        uint32_t   selCount = 0;
        VkPipeline pipeVis  = VK_NULL_HANDLE;
        VkPipeline pipeHid  = VK_NULL_HANDLE;

        [[nodiscard]] bool valid() const noexcept
        {
            return posVb != VK_NULL_HANDLE &&
                   selIb != VK_NULL_HANDLE &&
                   selCount > 0 &&
                   pipeVis != VK_NULL_HANDLE;
        }
    };

    /**
     * @brief Select coarse vs subdiv selection overlay inputs.
     * Chooses:
     * - position VB (unique verts vs subdiv shared verts)
     * - selection IB (mode-dependent, subdiv-dependent)
     * - visible/hidden pipelines (mode-dependent)
     */
    [[nodiscard]] SelDrawGeo selectSelGeometry(MeshGpuResources*   gpu,
                                               bool                useSubdiv,
                                               SelectionMode       mode,
                                               const SelPipelines& pipes) noexcept;

    // ============================================================
    // Wireframe edge geometry (coarse vs subdiv)
    // ============================================================

    /**
     * @brief VB/IB pair for drawing wireframe edges with vkCmdDrawIndexed().
     *
     * Assumptions (matches your selection selector pattern):
     * - Coarse: VB = uniqueVertBuffer(), IB = edgeIndexBuffer()
     * - Subdiv: VB = subdivSharedVertBuffer(), IB = subdivEdgeIndexBuffer()
     */
    struct WireDrawGeo
    {
        VkBuffer    posVb    = VK_NULL_HANDLE;
        VkBuffer    idxIb    = VK_NULL_HANDLE;
        uint32_t    idxCount = 0;
        VkIndexType idxType  = VK_INDEX_TYPE_UINT32;

        [[nodiscard]] bool valid() const noexcept
        {
            return posVb != VK_NULL_HANDLE && idxIb != VK_NULL_HANDLE && idxCount > 0;
        }
    };

    /**
     * @brief Selects edge index buffer for wire rendering.
     *
     * - Coarse: uniqueVertBuffer + edgeIndexBuffer
     * - Subdiv: subdivSharedVertBuffer + subdivPrimaryEdgeIndexBuffer
     */
    [[nodiscard]] WireDrawGeo selectWireGeometry(MeshGpuResources* gpu, bool useSubdiv) noexcept;

    // ============================================================
    // Grid orientation helper
    // ============================================================

    /**
     * @brief Returns a model matrix for drawing the grid oriented to the current view mode.
     *
     * Typical mapping:
     * - TOP/BOTTOM -> grid in XZ (rotate around X)
     * - FRONT/BACK -> grid in XY (identity / rotate around Y)
     * - LEFT/RIGHT -> grid in YZ (rotate around Y)
     *
     * Perspective is treated like TOP (XZ) by default (same as many DCCs).
     */
    [[nodiscard]] glm::mat4 gridModelFor(ViewMode mode) noexcept;

} // namespace render::geom
