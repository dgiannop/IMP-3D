// ============================================================
// RenderGeometry.cpp
// Helpers/RenderGeometry.cpp
// ============================================================
#include "RenderGeometry.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include "CoreTypes.hpp"
#include "MeshGpuResources.hpp"
#include "SceneMesh.hpp"

namespace render::geom
{
    GfxMeshGeometry selectGfxGeometry(SceneMesh* sm, MeshGpuResources* gpu) noexcept
    {
        GfxMeshGeometry out = {};

        if (!sm || !gpu)
            return out;

        const bool useSubdiv = (sm->subdivisionLevel() > 0);

        // =========================================================
        // Coarse
        // =========================================================
        if (!useSubdiv)
        {
            const uint32_t vtxCount = gpu->vertexCount();
            if (vtxCount == 0)
                return out;

            if (!gpu->polyVertBuffer().valid() ||
                !gpu->polyNormBuffer().valid() ||
                !gpu->polyMatIdBuffer().valid())
            {
                return out;
            }

            out.posBuffer   = gpu->polyVertBuffer().buffer();
            out.nrmBuffer   = gpu->polyNormBuffer().buffer();
            out.matBuffer   = gpu->polyMatIdBuffer().buffer();
            out.vertexCount = vtxCount;

            // UVs are optional.
            if (gpu->polyUvPosBuffer().valid())
                out.uvBuffer = gpu->polyUvPosBuffer().buffer();

            return out;
        }

        // =========================================================
        // Subdiv
        // =========================================================
        const uint32_t vtxCount = gpu->subdivPolyVertexCount();
        if (vtxCount == 0)
            return out;

        if (!gpu->subdivPolyVertBuffer().valid() ||
            !gpu->subdivPolyNormBuffer().valid() ||
            !gpu->subdivPolyMatIdBuffer().valid())
        {
            return out;
        }

        out.posBuffer   = gpu->subdivPolyVertBuffer().buffer();
        out.nrmBuffer   = gpu->subdivPolyNormBuffer().buffer();
        out.matBuffer   = gpu->subdivPolyMatIdBuffer().buffer();
        out.vertexCount = vtxCount;

        // UVs are optional.
        if (gpu->subdivPolyUvBuffer().valid())
            out.uvBuffer = gpu->subdivPolyUvBuffer().buffer();

        return out;
    }

    RtMeshGeometry selectRtGeometry(SceneMesh* sm) noexcept
    {
        RtMeshGeometry out = {};

        if (!sm)
            return out;

        MeshGpuResources* gpu = sm->gpu();
        if (!gpu)
            return out;

        const bool useSubdiv = (sm->subdivisionLevel() > 0);

        // =========================================================
        // Coarse
        // =========================================================
        if (!useSubdiv)
        {
            // BLAS build inputs
            if (gpu->uniqueVertCount() == 0 || !gpu->uniqueVertBuffer().valid())
                return out;
            if (gpu->coarseTriIndexCount() == 0 || !gpu->coarseTriIndexBuffer().valid())
                return out;

            // RT shader streams (expanded, face-varying)
            if (gpu->coarseRtPosCount() == 0 || !gpu->coarseRtPosBuffer().valid())
                return out;
            if (gpu->coarseRtCornerNrmCount() == 0 || !gpu->coarseRtCornerNrmBuffer().valid())
                return out;
            if (gpu->coarseRtCornerUvCount() == 0 || !gpu->coarseRtCornerUvBuffer().valid())
                return out;
            if (gpu->coarseRtTriCount() == 0 || !gpu->coarseRtTriIndexBuffer().valid())
                return out;

            out.buildPosBuffer = gpu->uniqueVertBuffer().buffer();
            out.buildPosCount  = gpu->uniqueVertCount();

            out.buildIndexBuffer = gpu->coarseTriIndexBuffer().buffer();
            out.buildIndexCount  = gpu->coarseTriIndexCount();

            out.shadePosBuffer = gpu->coarseRtPosBuffer().buffer();
            out.shadePosCount  = gpu->coarseRtPosCount();

            out.shadeNrmBuffer = gpu->coarseRtCornerNrmBuffer().buffer();
            out.shadeNrmCount  = gpu->coarseRtCornerNrmCount();

            out.shadeUvBuffer = gpu->coarseRtCornerUvBuffer().buffer();
            out.shadeUvCount  = gpu->coarseRtCornerUvCount();

            out.shaderIndexBuffer = gpu->coarseRtTriIndexBuffer().buffer();
            out.shaderTriCount    = gpu->coarseRtTriCount();

            return out;
        }

        // =========================================================
        // Subdiv
        // =========================================================

        // BLAS build inputs
        if (gpu->subdivSharedVertCount() == 0 || !gpu->subdivSharedVertBuffer().valid())
            return out;
        if (gpu->subdivSharedTriIndexCount() == 0 || !gpu->subdivSharedTriIndexBuffer().valid())
            return out;

        // RT shader streams (expanded, face-varying)
        if (gpu->subdivRtPosCount() == 0 || !gpu->subdivRtPosBuffer().valid())
            return out;
        if (gpu->subdivRtCornerNrmCount() == 0 || !gpu->subdivRtCornerNrmBuffer().valid())
            return out;
        if (gpu->subdivRtCornerUvCount() == 0 || !gpu->subdivRtCornerUvBuffer().valid())
            return out;
        if (gpu->subdivRtTriCount() == 0 || !gpu->subdivRtTriIndexBuffer().valid())
            return out;

        out.buildPosBuffer = gpu->subdivSharedVertBuffer().buffer();
        out.buildPosCount  = gpu->subdivSharedVertCount();

        out.buildIndexBuffer = gpu->subdivSharedTriIndexBuffer().buffer();
        out.buildIndexCount  = gpu->subdivSharedTriIndexCount();

        out.shadePosBuffer = gpu->subdivRtPosBuffer().buffer();
        out.shadePosCount  = gpu->subdivRtPosCount();

        out.shadeNrmBuffer = gpu->subdivRtCornerNrmBuffer().buffer();
        out.shadeNrmCount  = gpu->subdivRtCornerNrmCount();

        out.shadeUvBuffer = gpu->subdivRtCornerUvBuffer().buffer();
        out.shadeUvCount  = gpu->subdivRtCornerUvCount();

        out.shaderIndexBuffer = gpu->subdivRtTriIndexBuffer().buffer();
        out.shaderTriCount    = gpu->subdivRtTriCount();

        return out;
    }

    SelDrawGeo selectSelGeometry(MeshGpuResources*   gpu,
                                 bool                useSubdiv,
                                 SelectionMode       mode,
                                 const SelPipelines& pipes) noexcept
    {
        SelDrawGeo out = {};

        if (!gpu)
            return out;

        // ------------------------------------------------------------
        // Choose position stream (coarse unique vs subdiv shared)
        // ------------------------------------------------------------
        if (!useSubdiv)
        {
            if (gpu->uniqueVertCount() == 0 || !gpu->uniqueVertBuffer().valid())
                return out;

            out.posVb = gpu->uniqueVertBuffer().buffer();
        }
        else
        {
            if (gpu->subdivSharedVertCount() == 0 || !gpu->subdivSharedVertBuffer().valid())
                return out;

            out.posVb = gpu->subdivSharedVertBuffer().buffer();
        }

        // ------------------------------------------------------------
        // Choose selection index buffer + pipelines (mode-dependent)
        // ------------------------------------------------------------
        switch (mode)
        {
            case SelectionMode::VERTS: {
                out.pipeVis = pipes.vertVis;
                out.pipeHid = pipes.vertHid;

                if (!useSubdiv)
                {
                    if (gpu->selVertIndexCount() == 0 || !gpu->selVertIndexBuffer().valid())
                        return SelDrawGeo{};
                    out.selCount = gpu->selVertIndexCount();
                    out.selIb    = gpu->selVertIndexBuffer().buffer();
                }
                else
                {
                    if (gpu->subdivSelVertIndexCount() == 0 || !gpu->subdivSelVertIndexBuffer().valid())
                        return SelDrawGeo{};
                    out.selCount = gpu->subdivSelVertIndexCount();
                    out.selIb    = gpu->subdivSelVertIndexBuffer().buffer();
                }
            }
            break;

            case SelectionMode::EDGES: {
                out.pipeVis = pipes.edgeVis;
                out.pipeHid = pipes.edgeHid;

                if (!useSubdiv)
                {
                    if (gpu->selEdgeIndexCount() == 0 || !gpu->selEdgeIndexBuffer().valid())
                        return SelDrawGeo{};
                    out.selCount = gpu->selEdgeIndexCount();
                    out.selIb    = gpu->selEdgeIndexBuffer().buffer();
                }
                else
                {
                    if (gpu->subdivSelEdgeIndexCount() == 0 || !gpu->subdivSelEdgeIndexBuffer().valid())
                        return SelDrawGeo{};
                    out.selCount = gpu->subdivSelEdgeIndexCount();
                    out.selIb    = gpu->subdivSelEdgeIndexBuffer().buffer();
                }
            }
            break;

            case SelectionMode::POLYS: {
                out.pipeVis = pipes.polyVis;
                out.pipeHid = pipes.polyHid;

                if (!useSubdiv)
                {
                    if (gpu->selPolyIndexCount() == 0 || !gpu->selPolyIndexBuffer().valid())
                        return SelDrawGeo{};
                    out.selCount = gpu->selPolyIndexCount();
                    out.selIb    = gpu->selPolyIndexBuffer().buffer();
                }
                else
                {
                    if (gpu->subdivSelPolyIndexCount() == 0 || !gpu->subdivSelPolyIndexBuffer().valid())
                        return SelDrawGeo{};
                    out.selCount = gpu->subdivSelPolyIndexCount();
                    out.selIb    = gpu->subdivSelPolyIndexBuffer().buffer();
                }
            }
            break;

            default:
                return SelDrawGeo{};
        }

        if (out.pipeVis == VK_NULL_HANDLE)
            return SelDrawGeo{};

        return out;
    }

    WireDrawGeo selectWireGeometry(MeshGpuResources* gpu, bool useSubdiv) noexcept
    {
        WireDrawGeo out = {};

        if (!gpu)
            return out;

        if (!useSubdiv)
        {
            // Coarse edges
            if (gpu->edgeIndexCount() == 0)
                return out;

            if (!gpu->uniqueVertBuffer().valid() || !gpu->edgeIndexBuffer().valid())
                return out;

            out.posVb    = gpu->uniqueVertBuffer().buffer();
            out.idxIb    = gpu->edgeIndexBuffer().buffer();
            out.idxCount = gpu->edgeIndexCount();
            return out;
        }

        // Subdiv primary edges
        if (gpu->subdivPrimaryEdgeIndexCount() == 0)
            return out;

        if (!gpu->subdivSharedVertBuffer().valid() || !gpu->subdivPrimaryEdgeIndexBuffer().valid())
            return out;

        out.posVb    = gpu->subdivSharedVertBuffer().buffer();
        out.idxIb    = gpu->subdivPrimaryEdgeIndexBuffer().buffer();
        out.idxCount = gpu->subdivPrimaryEdgeIndexCount();
        return out;
    }

    glm::mat4 gridModelFor(ViewMode mode) noexcept
    {
        glm::mat4   m      = glm::mat4(1.0f);
        const float halfPi = glm::half_pi<float>();

        switch (mode)
        {
            case ViewMode::TOP:
                m = glm::mat4(1.0f);
                break;

            case ViewMode::BOTTOM:
                m = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
                break;

            case ViewMode::FRONT:
                m = glm::rotate(glm::mat4(1.0f), -halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
                break;

            case ViewMode::BACK:
                m = glm::rotate(glm::mat4(1.0f), +halfPi, glm::vec3(1.0f, 0.0f, 0.0f));
                break;

            case ViewMode::LEFT:
                m = glm::rotate(glm::mat4(1.0f), +halfPi, glm::vec3(0.0f, 0.0f, 1.0f));
                break;

            case ViewMode::RIGHT:
                m = glm::rotate(glm::mat4(1.0f), -halfPi, glm::vec3(0.0f, 0.0f, 1.0f));
                break;

            default:
                m = glm::mat4(1.0f);
                break;
        }

        return m;
    }

} // namespace render::geom
