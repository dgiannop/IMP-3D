//============================================================
// MeshGpuResources.hpp
//============================================================
#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "GpuBuffer.hpp"
#include "GpuResources.hpp"
#include "SysCounter.hpp"
#include "VulkanContext.hpp"

class SceneMesh;
class SysMesh;
class SubdivEvaluator;

class MeshGpuResources final : public GpuResources
{
public:
    MeshGpuResources(VulkanContext* ctx, SceneMesh* owner);
    ~MeshGpuResources() noexcept;

    MeshGpuResources(const MeshGpuResources&)            = delete;
    MeshGpuResources& operator=(const MeshGpuResources&) = delete;
    MeshGpuResources(MeshGpuResources&&)                 = delete;
    MeshGpuResources& operator=(MeshGpuResources&&)      = delete;

    void destroy() noexcept;

    /// Ensure GPU buffers match the owner's SysMesh (using counters).
    void update(const RenderFrameContext& fc); // override;

    // ---------------------------------------------------------
    // Coarse solid (corner-expanded triangle list, no indices)
    // ---------------------------------------------------------
    const GpuBuffer& polyVertBuffer() const
    {
        return m_polyVertBuffer;
    } // binding 0, vec3
    const GpuBuffer& polyNormBuffer() const
    {
        return m_polyNormBuffer;
    } // binding 1, vec3
    const GpuBuffer& polyUvPosBuffer() const
    {
        return m_polyUvBuffer;
    } // binding 2, vec2
    const GpuBuffer& polyMatIdBuffer() const
    {
        return m_polyMatIdBuffer;
    } // binding 3, uint32_t
    uint32_t vertexCount() const
    {
        return m_polyVertexCount;
    } // triCount*3

    // ---------------------------------------------------------
    // Coarse unique verts + edges
    //
    // uniqueVertBuffer:
    //  - 1:1 with SysMesh vertex slot indices (0..vert_buffer_size-1).
    //  - Includes "holes" (invalid slots) filled with (0,0,0) for stable indexing.
    // ---------------------------------------------------------
    const GpuBuffer& uniqueVertBuffer() const
    {
        return m_uniqueVertBuffer;
    } // vec3, index = SysMesh vertex index
    uint32_t uniqueVertCount() const
    {
        return m_uniqueVertCount;
    }

    const GpuBuffer& edgeIndexBuffer() const
    {
        return m_edgeIndexBuffer;
    } // uint32 indices into uniqueVertBuffer
    uint32_t edgeIndexCount() const
    {
        return m_edgeIndexCount;
    } // lineCount*2

    // ---------------------------------------------------------
    // Coarse RT triangles (shared)
    //
    // Triangle indices into uniqueVertBuffer. This is the "proper" coarse RT representation:
    //  - Vertex buffer: uniqueVertBuffer (shared vertex slots)
    //  - Index buffer:  coarseTriIndexBuffer (3 indices per triangle)
    // ---------------------------------------------------------
    const GpuBuffer& coarseTriIndexBuffer() const
    {
        return m_coarseTriIndexBuffer;
    }
    uint32_t coarseTriIndexCount() const
    {
        return m_coarseTriIndexCount;
    } // triCount*3

    // ---------------------------------------------------------
    // Coarse RT triangles (shader-readable padded)
    //
    // Vulkan RT shaders cannot safely read a runtime array of uvec3 (stride 12)
    // in buffer_reference / PhysicalStorageBuffer. Use uvec4 (stride 16) instead.
    //
    // This buffer is NOT used for BLAS builds.
    // It is only for closest-hit shading that wants to fetch triangle indices.
    // ---------------------------------------------------------
    const GpuBuffer& coarseRtTriIndexBuffer() const
    {
        return m_coarseRtTriIndexBuffer;
    }
    uint32_t coarseRtTriCount() const
    {
        return m_coarseRtTriCount;
    } // triCount (not *3)

    // ---------------------------------------------------------
    // Selection buffers (indexed into uniqueVertBuffer)
    // ---------------------------------------------------------
    const GpuBuffer& selVertIndexBuffer() const
    {
        return m_selVertIndexBuffer;
    }
    uint32_t selVertIndexCount() const
    {
        return m_selVertIndexCount;
    }

    const GpuBuffer& selEdgeIndexBuffer() const
    {
        return m_selEdgeIndexBuffer;
    }
    uint32_t selEdgeIndexCount() const
    {
        return m_selEdgeIndexCount;
    }

    const GpuBuffer& selPolyIndexBuffer() const
    {
        return m_selPolyIndexBuffer;
    }
    uint32_t selPolyIndexCount() const
    {
        return m_selPolyIndexCount;
    }

    // ---------------------------------------------------------
    // Subdiv solid (SysMesh-style: corner-expanded triangle list, no indices)
    // ---------------------------------------------------------
    const GpuBuffer& subdivPolyVertBuffer() const
    {
        return m_subdivPolyVertBuffer;
    }
    const GpuBuffer& subdivPolyNormBuffer() const
    {
        return m_subdivPolyNormBuffer;
    }
    const GpuBuffer& subdivPolyUvBuffer() const
    {
        return m_subdivPolyUvBuffer;
    }
    const GpuBuffer& subdivPolyMatIdBuffer() const
    {
        return m_subdivPolyMatIdBuffer;
    }
    uint32_t subdivPolyVertexCount() const
    {
        return m_subdivPolyVertexCount;
    } // triCount*3 (corner-expanded)

    // ---------------------------------------------------------
    // Subdiv shared representation (aux/debug/compute – NOT used for solid shading)
    // ---------------------------------------------------------
    const GpuBuffer& subdivSharedVertBuffer() const
    {
        return m_subdivSharedVertBuffer;
    }
    uint32_t subdivSharedVertCount() const
    {
        return m_subdivSharedVertCount;
    }

    const GpuBuffer& subdivSharedTriIndexBuffer() const
    {
        return m_subdivSharedTriIndexBuffer;
    }
    uint32_t subdivSharedTriIndexCount() const
    {
        return m_subdivSharedTriIndexCount;
    }

    // ---------------------------------------------------------
    // Subdiv RT triangles (shader-readable padded)
    // ---------------------------------------------------------
    const GpuBuffer& subdivRtTriIndexBuffer() const
    {
        return m_subdivRtTriIndexBuffer;
    }
    uint32_t subdivRtTriCount() const
    {
        return m_subdivRtTriCount;
    }

    // ---------------------------------------------------------
    // Coarse RT positions (shader-readable padded vec4)
    //  - 1:1 with SysMesh vertex slot indices (same indexing as uniqueVertBuffer)
    // ---------------------------------------------------------
    const GpuBuffer& coarseRtPosBuffer() const
    {
        return m_coarseRtPosBuffer;
    }
    uint32_t coarseRtPosCount() const
    {
        return m_coarseRtPosCount;
    }

    // ---------------------------------------------------------
    // Subdiv RT positions (shader-readable padded vec4)
    //  - 1:1 with subdiv shared vertex indices (same indexing as subdivSharedVertBuffer)
    // ---------------------------------------------------------
    const GpuBuffer& subdivRtPosBuffer() const
    {
        return m_subdivRtPosBuffer;
    }
    uint32_t subdivRtPosCount() const
    {
        return m_subdivRtPosCount;
    }

    // ---------------------------------------------------------
    // Coarse RT CORNER normals (shader-readable padded vec4)
    //  - 3 normals per triangle, in RT primitive order.
    //  - Index: corner = primId*3 + c
    //  - Count = coarseRtTriCount()*3
    // ---------------------------------------------------------
    const GpuBuffer& coarseRtCornerNrmBuffer() const
    {
        return m_coarseRtCornerNrmBuffer;
    }
    uint32_t coarseRtCornerNrmCount() const
    {
        return m_coarseRtCornerNrmCount;
    }

    // ---------------------------------------------------------
    // Subdiv RT CORNER normals (shader-readable padded vec4)
    //  - 3 normals per triangle, in RT primitive order.
    //  - Index: corner = primId*3 + c
    //  - Count = subdivRtTriCount()*3
    // ---------------------------------------------------------
    const GpuBuffer& subdivRtCornerNrmBuffer() const
    {
        return m_subdivRtCornerNrmBuffer;
    }
    uint32_t subdivRtCornerNrmCount() const
    {
        return m_subdivRtCornerNrmCount;
    }

    // ---------------------------------------------------------
    // Subdiv primary edges (coarse-derived), line-list indices
    // (Renderer decides which position buffer to use; usually subdivSharedVertBuffer)
    // ---------------------------------------------------------
    const GpuBuffer& subdivPrimaryEdgeIndexBuffer() const
    {
        return m_subdivPrimaryEdgeIndexBuffer;
    }
    uint32_t subdivPrimaryEdgeIndexCount() const
    {
        return m_subdivPrimaryEdgeIndexCount;
    }

    const GpuBuffer& coarseRtCornerUvBuffer() const
    {
        return m_coarseRtCornerUvBuffer;
    }
    uint32_t coarseRtCornerUvCount() const
    {
        return m_coarseRtCornerUvCount;
    }

    const GpuBuffer& subdivRtCornerUvBuffer() const
    {
        return m_subdivRtCornerUvBuffer;
    }
    uint32_t subdivRtCornerUvCount() const
    {
        return m_subdivRtCornerUvCount;
    }

    // ---------------------------------------------------------
    // Subdiv selection (indices into subdivSharedVertBuffer)
    // ---------------------------------------------------------
    const GpuBuffer& subdivSelVertIndexBuffer() const
    {
        return m_subdivSelVertIndexBuffer;
    }
    uint32_t subdivSelVertIndexCount() const
    {
        return m_subdivSelVertIndexCount;
    }

    const GpuBuffer& subdivSelEdgeIndexBuffer() const
    {
        return m_subdivSelEdgeIndexBuffer;
    }
    uint32_t subdivSelEdgeIndexCount() const
    {
        return m_subdivSelEdgeIndexCount;
    }

    const GpuBuffer& subdivSelPolyIndexBuffer() const
    {
        return m_subdivSelPolyIndexBuffer;
    }
    uint32_t subdivSelPolyIndexCount() const
    {
        return m_subdivSelPolyIndexCount;
    }

private:
    VulkanContext* m_ctx   = nullptr;
    SceneMesh*     m_owner = nullptr;

    // ---------------------------------------------------------
    // Coarse solid (corner-expanded triangle list)
    // ---------------------------------------------------------
    GpuBuffer m_polyVertBuffer;      // binding 0, vec3
    GpuBuffer m_polyNormBuffer;      // binding 1, vec3
    GpuBuffer m_polyUvBuffer;        // binding 2, vec2
    GpuBuffer m_polyMatIdBuffer;     // binding 3, uint32_t
    uint32_t  m_polyVertexCount = 0; // triCount*3

    // ---------------------------------------------------------
    // Coarse unique vertices & edges
    // ---------------------------------------------------------
    GpuBuffer m_uniqueVertBuffer; // vec3 positions; index = SysMesh vertex slot id
    uint32_t  m_uniqueVertCount = 0;

    GpuBuffer m_edgeIndexBuffer; // uint32 indices into m_uniqueVertBuffer (line list)
    uint32_t  m_edgeIndexCount = 0;

    // ---------------------------------------------------------
    // Coarse RT (shared triangle indices into m_uniqueVertBuffer)
    // ---------------------------------------------------------
    GpuBuffer m_coarseTriIndexBuffer; // uint32 indices (3 per tri)
    uint32_t  m_coarseTriIndexCount = 0;

    // ---------------------------------------------------------
    // Selection buffers (indexed into uniqueVertBuffer)
    // ---------------------------------------------------------
    GpuBuffer m_selVertIndexBuffer;
    uint32_t  m_selVertIndexCount = 0;

    GpuBuffer m_selEdgeIndexBuffer;
    uint32_t  m_selEdgeIndexCount = 0;

    GpuBuffer m_selPolyIndexBuffer;
    uint32_t  m_selPolyIndexCount = 0;

    // ---------------------------------------------------------
    // Coarse RT (shader-readable padded triangle indices)
    //
    // Each triangle is stored as 16 bytes: uvec4(a,b,c,0).
    // This avoids runtime-array stride/alignment issues (uvec3 stride=12).
    // ---------------------------------------------------------
    GpuBuffer m_coarseRtTriIndexBuffer; // uint32[4] per tri (uvec4)
    uint32_t  m_coarseRtTriCount = 0;   // triCount

    // ---------------------------------------------------------
    // Coarse RT position buffer (vec4 padded, shader-readable)
    // ---------------------------------------------------------
    GpuBuffer m_coarseRtPosBuffer; // vec4 positions, device address
    uint32_t  m_coarseRtPosCount = 0;

    // Coarse RT CORNER normal buffer (vec4 padded, shader-readable)
    GpuBuffer m_coarseRtCornerNrmBuffer;
    uint32_t  m_coarseRtCornerNrmCount = 0;

    // Subdiv RT normal buffer (vec4 padded, shader-readable)
    GpuBuffer m_subdivRtCornerNrmBuffer;
    uint32_t  m_subdivRtCornerNrmCount = 0;

    // ---------------------------------------------------------
    // Subdiv RT position buffer (vec4 padded, shader-readable)
    // ---------------------------------------------------------
    GpuBuffer m_subdivRtPosBuffer; // vec4 positions, device address
    uint32_t  m_subdivRtPosCount = 0;

    // ---------------------------------------------------------
    // Subdiv solid (corner-expanded triangle list, SysMesh semantics)
    // ---------------------------------------------------------
    GpuBuffer m_subdivPolyVertBuffer;      // binding 0, vec3
    GpuBuffer m_subdivPolyNormBuffer;      // binding 1, vec3
    GpuBuffer m_subdivPolyUvBuffer;        // binding 2, vec2
    GpuBuffer m_subdivPolyMatIdBuffer;     // binding 3, uint32
    uint32_t  m_subdivPolyVertexCount = 0; // triCount*3 (corner-expanded)

    // ---------------------------------------------------------
    // Subdiv shared representation (NOT used for solid shading)
    // ---------------------------------------------------------
    GpuBuffer m_subdivSharedVertBuffer; // vec3 positions
    uint32_t  m_subdivSharedVertCount = 0;

    GpuBuffer m_subdivSharedTriIndexBuffer; // uint32 indices (3 per tri)
    uint32_t  m_subdivSharedTriIndexCount = 0;

    // Shader-readable padded triangles: uvec4(a,b,c,0)
    GpuBuffer m_subdivRtTriIndexBuffer;
    uint32_t  m_subdivRtTriCount = 0;

    // Coarse RT CORNER uv buffer (vec4 padded, shader-readable)
    GpuBuffer m_coarseRtCornerUvBuffer;
    uint32_t  m_coarseRtCornerUvCount = 0;

    // Subdiv RT CORNER uv buffer (vec4 padded, shader-readable)
    GpuBuffer m_subdivRtCornerUvBuffer;
    uint32_t  m_subdivRtCornerUvCount = 0;

    // ---------------------------------------------------------
    // Subdiv primary edges (coarse-derived)
    // ---------------------------------------------------------
    GpuBuffer m_subdivPrimaryEdgeIndexBuffer; // uint32 line list (2 per edge)
    uint32_t  m_subdivPrimaryEdgeIndexCount = 0;

    // ---------------------------------------------------------
    // Subdiv selection buffers (indices into subdivSharedVertBuffer)
    // ---------------------------------------------------------
    GpuBuffer m_subdivSelVertIndexBuffer;
    uint32_t  m_subdivSelVertIndexCount = 0;

    GpuBuffer m_subdivSelEdgeIndexBuffer;
    uint32_t  m_subdivSelEdgeIndexCount = 0;

    GpuBuffer m_subdivSelPolyIndexBuffer;
    uint32_t  m_subdivSelPolyIndexCount = 0;

    // Current cached subdivision level (0 = coarse path)
    int m_cachedSubdivLevel = 0;

    // ---------------------------------------------------------
    // Change monitors
    // ---------------------------------------------------------
    SysMonitor m_topologyMonitor;
    SysMonitor m_deformMonitor;
    SysMonitor m_selectionMonitor;

private:
    void fullRebuild(const RenderFrameContext& fc, const SysMesh* sys);
    void updateDeformBuffers(const RenderFrameContext& fc, const SysMesh* sys);
    void updateSelectionBuffers(const RenderFrameContext& fc, const SysMesh* sys);

    // --- Subdiv
    void updateSelectionBuffersSubdiv(const RenderFrameContext& fc, const SysMesh* sys, int level);

    std::vector<uint32_t> flattenEdgePairs(const std::vector<std::pair<uint32_t, uint32_t>>& edges);

    void fullRebuildSubdiv(const RenderFrameContext& fc, const SysMesh* sys, int level);
    void updateSubdivDeform(const RenderFrameContext& fc, const SysMesh* sys, int level);

    void buildSubdivCornerExpanded(SubdivEvaluator&        subdiv,
                                   std::vector<glm::vec3>& outPos,
                                   std::vector<glm::vec3>& outNrm,
                                   std::vector<glm::vec2>& outUv,
                                   std::vector<uint32_t>&  outMat);

private:
    static constexpr VkDeviceSize kCapacity64KiB = 64ull * 1024ull;

    template<typename T>
    void updateOrRecreate(const RenderFrameContext& fc,
                          GpuBuffer&                buffer,
                          const std::vector<T>&     data,
                          VkBufferUsageFlags        usage,
                          VkDeviceSize              initialCapacity = kCapacity64KiB,
                          bool                      deviceAddress   = false);
};
