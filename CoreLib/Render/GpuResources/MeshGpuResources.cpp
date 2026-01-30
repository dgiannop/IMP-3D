//============================================================
// MeshGpuResources.cpp
//============================================================
#include "MeshGpuResources.hpp"

#include <Sysmesh.hpp>
#include <algorithm>

#include "MeshUtilities.hpp"
#include "SceneMesh.hpp"
#include "VkUtilities.hpp"

// -------------------------------------------------------------
// Template helper
// -------------------------------------------------------------
template<typename T>
void MeshGpuResources::updateOrRecreate(const RenderFrameContext& fc,
                                        GpuBuffer&                buffer,
                                        const std::vector<T>&     data,
                                        VkBufferUsageFlags        usage,
                                        VkDeviceSize              initialCapacity,
                                        bool                      deviceAddress)
{
    if (data.empty() || !m_ctx || !fc.cmd)
        return;

    const VkDeviceSize size = VkDeviceSize(sizeof(T)) * VkDeviceSize(data.size());
    if (size == 0)
        return;

    VkBufferUsageFlags finalUsage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (deviceAddress)
        finalUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    // If no deferred queue is available, we must be conservative.
    // (You can also choose to "leak" in this case, but I'd rather just early-out.)
    DeferredDeletion* deferred = fc.deferred;
    const uint32_t    fi       = fc.frameIndex;

    // ------------------------------------------------------------
    // Helper: record copy from a fresh staging buffer.
    // Staging is destroyed later via deferred deletion.
    // ------------------------------------------------------------
    auto recordCopyWithDeferredStaging =
        [&](VkBuffer dstBuf, VkDeviceSize dstOffset, const void* srcData, VkDeviceSize bytes) -> bool {
        if (!dstBuf || !srcData || bytes == 0)
            return false;

        GpuBuffer staging;
        staging.create(m_ctx->device,
                       m_ctx->physicalDevice,
                       bytes,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       /*persistentMap*/ true);

        if (!staging.valid())
            return false;

        staging.upload(srcData, bytes);

        VkBufferCopy cpy = {};
        cpy.srcOffset    = 0;
        cpy.dstOffset    = dstOffset;
        cpy.size         = bytes;

        vkCmdCopyBuffer(fc.cmd, staging.buffer(), dstBuf, 1, &cpy);

        // Defer staging destruction until frame slot is safe again.
        if (deferred)
        {
            deferred->enqueue(fi, [st = std::move(staging)]() mutable {
                // destructor runs when lambda is destroyed during flush
            });
        }
        else
        {
            // No deferred deletion available => safest is to keep it alive (leak) or hard-sync.
            // Prefer leak in debug if you hit this path.
            static std::vector<GpuBuffer> s_leaked;
            s_leaked.push_back(std::move(staging));
        }

        return true;
    };

    // Fast path: reuse existing device-local buffer
    if (buffer.valid() && size <= buffer.size())
    {
        (void)recordCopyWithDeferredStaging(buffer.buffer(), 0, data.data(), size);
        return;
    }

    // Growth policy
    const VkDeviceSize capacity =
        buffer.valid()
            ? std::max(size, buffer.size() + buffer.size() / 2)
            : std::max(size, initialCapacity);

    // Defer destruction of the old device-local buffer (instead of leaking forever)
    if (buffer.valid())
    {
        if (deferred)
        {
            deferred->enqueue(fi, [old = std::move(buffer)]() mutable {
                // RAII destroys on lambda destruction/flush
            });
        }
        else
        {
            static std::vector<GpuBuffer> s_leakedDeviceLocal;
            s_leakedDeviceLocal.push_back(std::move(buffer));
        }

        buffer = {}; // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    }

    buffer = vkutil::createDeviceLocalBufferEmpty(*m_ctx, capacity, finalUsage, deviceAddress);
    if (!buffer.valid())
        return;

    (void)recordCopyWithDeferredStaging(buffer.buffer(), 0, data.data(), size);
}

// -------------------------------------------------------------
// Constructor / Destructor
// -------------------------------------------------------------
MeshGpuResources::MeshGpuResources(VulkanContext* ctx, SceneMesh* owner) :
    m_ctx{ctx},
    m_owner{owner},
    m_topologyMonitor{m_owner->sysMesh()->topology_counter()},
    m_deformMonitor{m_owner->sysMesh()->deform_counter()},
    m_selectionMonitor{m_owner->sysMesh()->select_counter()}
{
}

MeshGpuResources::~MeshGpuResources() noexcept
{
    destroy();
}

void MeshGpuResources::destroy() noexcept
{
    // Coarse solid
    m_polyVertBuffer.destroy();
    m_polyNormBuffer.destroy();
    m_polyUvBuffer.destroy();
    m_polyMatIdBuffer.destroy();
    m_polyVertexCount = 0;

    // Coarse shared + edges
    m_uniqueVertBuffer.destroy();
    m_uniqueVertCount = 0;

    m_edgeIndexBuffer.destroy();
    m_edgeIndexCount = 0;

    // Coarse RT
    m_coarseTriIndexBuffer.destroy();
    m_coarseTriIndexCount = 0;

    m_coarseRtTriIndexBuffer.destroy();
    m_coarseRtTriCount = 0;

    m_coarseRtPosBuffer.destroy();
    m_coarseRtPosCount = 0;

    m_coarseRtCornerNrmBuffer.destroy();
    m_coarseRtCornerNrmCount = 0;

    m_coarseRtCornerUvBuffer.destroy();
    m_coarseRtCornerUvCount = 0;

    m_subdivRtCornerUvBuffer.destroy();
    m_subdivRtCornerUvCount = 0;

    // SubDiv RT
    m_subdivRtTriIndexBuffer.destroy();
    m_subdivRtTriCount = 0;

    m_subdivRtPosBuffer.destroy();
    m_subdivRtPosCount = 0;

    m_subdivRtCornerNrmBuffer.destroy();
    m_subdivRtCornerNrmCount = 0;

    m_coarseRtMatIdBuffer.destroy();
    m_coarseRtMatIdCount = 0;

    m_subdivRtMatIdBuffer.destroy();
    m_subdivRtMatIdCount = 0;

    // Selection (coarse)
    m_selVertIndexBuffer.destroy();
    m_selEdgeIndexBuffer.destroy();
    m_selPolyIndexBuffer.destroy();

    m_selVertIndexCount = 0;
    m_selEdgeIndexCount = 0;
    m_selPolyIndexCount = 0;

    // Subdiv solid (corner-expanded)
    m_subdivPolyVertBuffer.destroy();
    m_subdivPolyNormBuffer.destroy();
    m_subdivPolyUvBuffer.destroy();
    m_subdivPolyMatIdBuffer.destroy();
    m_subdivPolyVertexCount = 0;

    // Subdiv shared (aux/debug)
    m_subdivSharedVertBuffer.destroy();
    m_subdivSharedTriIndexBuffer.destroy();
    m_subdivSharedVertCount     = 0;
    m_subdivSharedTriIndexCount = 0;

    // Subdiv primary edges
    m_subdivPrimaryEdgeIndexBuffer.destroy();
    m_subdivPrimaryEdgeIndexCount = 0;

    // Subdiv selection
    m_subdivSelVertIndexBuffer.destroy();
    m_subdivSelEdgeIndexBuffer.destroy();
    m_subdivSelPolyIndexBuffer.destroy();

    m_subdivSelVertIndexCount = 0;
    m_subdivSelEdgeIndexCount = 0;
    m_subdivSelPolyIndexCount = 0;

    m_cachedSubdivLevel = 0;
}

// ============================================================================
// UPDATE ENTRY (call from prePassRender; render() should only bind+draw)
// ============================================================================

void MeshGpuResources::update(const RenderFrameContext& fc)
{
    const SysMesh* sys = m_owner ? m_owner->sysMesh() : nullptr;

    if (!sys || !m_ctx || !fc.cmd)
        return;

    const int  level        = m_owner->subdivisionLevel();
    const bool levelChanged = (level != m_cachedSubdivLevel);

    const bool topoChanged   = m_topologyMonitor.changed();
    const bool deformChanged = m_deformMonitor.changed();
    const bool selectChanged = m_selectionMonitor.changed();

    if (!topoChanged && !deformChanged && !selectChanged && !levelChanged)
        return;

    // ---------------------------------------------------------
    // Subdivision path
    // ---------------------------------------------------------
    if (level > 0)
    {
        if (topoChanged || levelChanged)
        {
            fullRebuildSubdiv(fc, sys, level);
            updateSelectionBuffersSubdiv(fc, sys, level);
            m_cachedSubdivLevel = level;
            return;
        }

        if (deformChanged)
            updateSubdivDeform(fc, sys, level);

        if (selectChanged)
            updateSelectionBuffersSubdiv(fc, sys, level);

        m_cachedSubdivLevel = level;
        return;
    }

    // ---------------------------------------------------------
    // Coarse path
    // ---------------------------------------------------------
    if (levelChanged)
    {
        // switching subdiv -> coarse: must rebuild coarse buffers
        fullRebuild(fc, sys);
        updateSelectionBuffers(fc, sys);
        m_cachedSubdivLevel = 0;
        return;
    }

    m_cachedSubdivLevel = 0;

    if (selectChanged && !topoChanged && !deformChanged)
    {
        updateSelectionBuffers(fc, sys);
        return;
    }

    if (deformChanged && !topoChanged)
    {
        updateDeformBuffers(fc, sys);
        if (selectChanged)
            updateSelectionBuffers(fc, sys);
        return;
    }

    if (topoChanged)
    {
        fullRebuild(fc, sys);
        if (selectChanged)
            updateSelectionBuffers(fc, sys);
        return;
    }
}

// ============================================================================
// COARSE TOPOLOGY REBUILD (records uploads into cmd; adds the *minimum* barriers)
// ============================================================================

void MeshGpuResources::fullRebuild(const RenderFrameContext& fc, const SysMesh* sys)
{
    if (!sys || !m_ctx || !fc.cmd)
        return;

    // Extract corner-expanded triangle streams for solid draw (no indices)
    const MeshData              tri     = extractMeshData(sys);
    const std::vector<uint32_t> edgeIdx = extractMeshEdgeIndices(sys);

    m_polyVertexCount = static_cast<uint32_t>(tri.verts.size());
    m_edgeIndexCount  = static_cast<uint32_t>(edgeIdx.size());

    // Solid draw vertex streams (corner-expanded)
    updateOrRecreate(fc, m_polyVertBuffer, tri.verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    updateOrRecreate(fc, m_polyNormBuffer, tri.norms, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    updateOrRecreate(fc, m_polyUvBuffer, tri.uvPos, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    updateOrRecreate(fc, m_polyMatIdBuffer, tri.matIds, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // Unique per-slot positions (shared)
    const uint32_t slotCount = sys->vert_buffer_size();
    m_uniqueVertCount        = slotCount;

    std::vector<glm::vec3> uniqueVerts(slotCount, glm::vec3{0.0f});
    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
            uniqueVerts[vi] = sys->vert_position(static_cast<int32_t>(vi));
    }

    updateOrRecreate(fc,
                     m_uniqueVertBuffer,
                     uniqueVerts,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // Edge indices (into uniqueVerts)
    updateOrRecreate(fc, m_edgeIndexBuffer, edgeIdx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // BLAS build triangle indices (tight uint32, into uniqueVerts)
    const std::vector<uint32_t> triIdx = extractMeshTriIndices(sys);
    m_coarseTriIndexCount              = static_cast<uint32_t>(triIdx.size());

    updateOrRecreate(fc,
                     m_coarseTriIndexBuffer,
                     triIdx,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // Shader-readable triangle indices (uvec4 packed as uint32 stream: a,b,c,0)
    m_coarseRtTriCount = 0;
    if (!triIdx.empty() && (triIdx.size() % 3u) == 0u)
    {
        const uint32_t triCount = static_cast<uint32_t>(triIdx.size() / 3u);
        m_coarseRtTriCount      = triCount;

        std::vector<uint32_t> triIdx4;
        triIdx4.resize(size_t(triCount) * 4ull);

        for (uint32_t t = 0; t < triCount; ++t)
        {
            triIdx4[t * 4u + 0u] = triIdx[t * 3u + 0u];
            triIdx4[t * 4u + 1u] = triIdx[t * 3u + 1u];
            triIdx4[t * 4u + 2u] = triIdx[t * 3u + 2u];
            triIdx4[t * 4u + 3u] = 0u;
        }

        updateOrRecreate(fc,
                         m_coarseRtTriIndexBuffer,
                         triIdx4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    // Shader-readable positions (vec4 padded, per unique vert slot)
    m_coarseRtPosCount = slotCount;

    std::vector<glm::vec4> uniqueVerts4(slotCount, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
        {
            const glm::vec3 p = sys->vert_position(static_cast<int32_t>(vi));
            uniqueVerts4[vi]  = glm::vec4(p, 1.0f);
        }
    }

    updateOrRecreate(fc,
                     m_coarseRtPosBuffer,
                     uniqueVerts4,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // Per-corner normals/uvs for RT shading (must match RT triangle order)
    m_coarseRtCornerNrmCount = 0;
    m_coarseRtCornerUvCount  = 0;

    if (m_coarseRtTriCount > 0)
    {
        // We assume extractMeshData() outputs tri streams in the same triangle order as extractMeshTriIndices().
        if (tri.norms.size() == triIdx.size())
        {
            std::vector<glm::vec4> nrm4(tri.norms.size());
            for (size_t i = 0; i < tri.norms.size(); ++i)
                nrm4[i] = glm::vec4(tri.norms[i], 0.0f);

            m_coarseRtCornerNrmCount = static_cast<uint32_t>(nrm4.size());

            updateOrRecreate(fc,
                             m_coarseRtCornerNrmBuffer,
                             nrm4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }

        if (tri.uvPos.size() == triIdx.size())
        {
            std::vector<glm::vec4> uv4(tri.uvPos.size());
            for (size_t i = 0; i < tri.uvPos.size(); ++i)
                uv4[i] = glm::vec4(tri.uvPos[i], 0.0f, 0.0f);

            m_coarseRtCornerUvCount = static_cast<uint32_t>(uv4.size());

            updateOrRecreate(fc,
                             m_coarseRtCornerUvBuffer,
                             uv4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
    }

    // ---------------------------------------------------------
    // NEW: RT per-triangle material IDs (uint32, indexed by primId)
    // We derive it from corner-expanded tri.matIds (one id per triangle).
    // ---------------------------------------------------------
    m_coarseRtMatIdCount = 0;

    if (m_coarseRtTriCount > 0)
    {
        const size_t triCount        = size_t(m_coarseRtTriCount);
        const size_t expectedCorners = triCount * 3ull;

        if (tri.matIds.size() == expectedCorners)
        {
            std::vector<uint32_t> matPerTri;
            matPerTri.resize(triCount);

            for (size_t t = 0; t < triCount; ++t)
                matPerTri[t] = tri.matIds[t * 3ull + 0ull];

            m_coarseRtMatIdCount = static_cast<uint32_t>(matPerTri.size());

            updateOrRecreate(fc,
                             m_coarseRtMatIdBuffer,
                             matPerTri,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
    }

    // --------------------------------------------------------------------
    // Barriers:
    //  - solid/unique vertex streams are read by vertex input
    //  - edge/tri indices are read by vertex input
    //  - BLAS build reads unique verts + tri indices
    //  - RT shaders read storage buffers (pos/tri/nrm/uv/mat)
    // --------------------------------------------------------------------
    vkutil::barrierTransferToVertexAttributeRead(fc.cmd);
    vkutil::barrierTransferToIndexRead(fc.cmd);

    vkutil::barrierTransferToAsBuildRead(fc.cmd);
    vkutil::barrierTransferToRtShaderRead(fc.cmd);
}

// ============================================================================
// COARSE DEFORM UPDATE (positions + normals; keeps topology-dependent buffers)
// ============================================================================

void MeshGpuResources::updateDeformBuffers(const RenderFrameContext& fc, const SysMesh* sys)
{
    if (!sys || !m_ctx || !fc.cmd)
        return;

    // 1) Unique slot verts (edge/selection + BLAS build input)
    const uint32_t slotCount = sys->vert_buffer_size();
    m_uniqueVertCount        = slotCount;

    std::vector<glm::vec3> uniqueVerts(slotCount, glm::vec3{0.0f});
    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
            uniqueVerts[vi] = sys->vert_position(static_cast<int32_t>(vi));
    }

    updateOrRecreate(fc,
                     m_uniqueVertBuffer,
                     uniqueVerts,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // 2) Corner-expanded solid positions
    std::vector<glm::vec3> triVerts = extractTriPositionsOnly(sys);
    m_polyVertexCount               = static_cast<uint32_t>(triVerts.size());

    updateOrRecreate(fc, m_polyVertBuffer, triVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);

    // 3) Corner-expanded normals (optional but recommended for correct lighting while moving)
    std::vector<glm::vec3> norms = extractPolyNormasOnly(sys);
    if (!norms.empty())
        updateOrRecreate(fc, m_polyNormBuffer, norms, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);

    // 4) RT position buffer (vec4 padded)
    m_coarseRtPosCount = slotCount;

    std::vector<glm::vec4> uniqueVerts4(slotCount, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
        {
            const glm::vec3 p = sys->vert_position(static_cast<int32_t>(vi));
            uniqueVerts4[vi]  = glm::vec4(p, 1.0f);
        }
    }

    updateOrRecreate(fc,
                     m_coarseRtPosBuffer,
                     uniqueVerts4,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // Barriers: vertex input reads + BLAS build reads + RT shader reads
    vkutil::barrierTransferToVertexAttributeRead(fc.cmd);
    vkutil::barrierTransferToAsBuildRead(fc.cmd);
    vkutil::barrierTransferToRtShaderRead(fc.cmd);
}

// ============================================================================
// COARSE SELECTION UPDATE (indices only)
// ============================================================================

void MeshGpuResources::updateSelectionBuffers(const RenderFrameContext& fc, const SysMesh* sys)
{
    if (!sys || !m_ctx || !fc.cmd)
        return;

    std::vector<uint32_t> selV = extractSelectedVertices(sys);
    std::vector<uint32_t> selE = extractSelectedEdges(sys);
    std::vector<uint32_t> selP = extractSelectedPolyTriangles(sys);

    m_selVertIndexCount = static_cast<uint32_t>(selV.size());
    m_selEdgeIndexCount = static_cast<uint32_t>(selE.size());
    m_selPolyIndexCount = static_cast<uint32_t>(selP.size());

    updateOrRecreate(fc, m_selVertIndexBuffer, selV, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kCapacity64KiB);
    updateOrRecreate(fc, m_selEdgeIndexBuffer, selE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kCapacity64KiB);
    updateOrRecreate(fc, m_selPolyIndexBuffer, selP, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kCapacity64KiB);

    vkutil::barrierTransferToIndexRead(fc.cmd);
}

// ============================================================================
// Subdiv helpers
// ============================================================================

std::vector<uint32_t>
MeshGpuResources::flattenEdgePairs(const std::vector<std::pair<uint32_t, uint32_t>>& edges)
{
    std::vector<uint32_t> out;
    out.reserve(edges.size() * 2);

    for (const auto& e : edges)
    {
        out.push_back(e.first);
        out.push_back(e.second);
    }

    return out;
}

void MeshGpuResources::buildSubdivCornerExpanded(SubdivEvaluator&        subdiv,
                                                 std::vector<glm::vec3>& outPos,
                                                 std::vector<glm::vec3>& outNrm,
                                                 std::vector<glm::vec2>& outUv,
                                                 std::vector<uint32_t>&  outMat)
{
    const auto verts = subdiv.vertices();
    const auto norms = subdiv.normals();
    const auto tris  = subdiv.triangleIndices();

    const auto& uvs   = subdiv.uvs();
    const auto& triUV = subdiv.triangleUVIndices();

    const auto triMat = subdiv.triangleMaterialIds();

    if (tris.empty() || (tris.size() % 3) != 0)
    {
        outPos.clear();
        outNrm.clear();
        outUv.clear();
        outMat.clear();
        return;
    }

    const size_t triCount    = tris.size() / 3;
    const size_t cornerCount = triCount * 3;

    // Expect per-corner UV indices and per-tri material ids.
    if (triUV.size() != tris.size() || triMat.size() != triCount)
    {
        outPos.clear();
        outNrm.clear();
        outUv.clear();
        outMat.clear();
        return;
    }

    outPos.resize(cornerCount);
    outNrm.resize(cornerCount);
    outUv.resize(cornerCount);
    outMat.resize(cornerCount);

    for (size_t tri = 0; tri < triCount; ++tri)
    {
        const uint32_t mat = triMat[tri];

        for (size_t c = 0; c < 3; ++c)
        {
            const size_t corner = tri * 3 + c;

            const uint32_t vi = tris[corner];
            const uint32_t ui = triUV[corner];

            outPos[corner] = (vi < verts.size()) ? verts[vi] : glm::vec3{0.0f};
            outNrm[corner] = (vi < norms.size()) ? norms[vi] : glm::vec3{0.0f, 1.0f, 0.0f};
            outUv[corner]  = (ui < uvs.size()) ? uvs[ui] : glm::vec2{0.0f};
            outMat[corner] = mat;
        }
    }
}

// ============================================================================
// SUBDIV FULL REBUILD (topology/level)
// ============================================================================

void MeshGpuResources::fullRebuildSubdiv(const RenderFrameContext& fc, const SysMesh* sys, int level)
{
    if (!sys || !m_ctx || !fc.cmd)
        return;

    SubdivEvaluator* subdiv = m_owner ? m_owner->subdiv() : nullptr;
    if (!subdiv)
        return;

    // Topology rebuild + refine to level + evaluate products
    subdiv->onTopologyChanged(const_cast<SysMesh*>(sys), level);

    // ---------------------------------------------------------
    // A) Subdiv shared representation (used for BLAS + misc)
    // ---------------------------------------------------------
    {
        const auto verts        = subdiv->vertices();
        m_subdivSharedVertCount = static_cast<uint32_t>(verts.size());

        std::vector<glm::vec3> tmp;
        tmp.assign(verts.begin(), verts.end());

        updateOrRecreate(fc,
                         m_subdivSharedVertBuffer,
                         tmp,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);

        // RT pos (vec4 padded) from the same shared verts
        m_subdivRtPosCount = m_subdivSharedVertCount;

        std::vector<glm::vec4> tmp4(tmp.size());
        for (size_t i = 0; i < tmp.size(); ++i)
            tmp4[i] = glm::vec4(tmp[i], 1.0f);

        updateOrRecreate(fc,
                         m_subdivRtPosBuffer,
                         tmp4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    {
        const auto triIdx           = subdiv->triangleIndices();
        m_subdivSharedTriIndexCount = static_cast<uint32_t>(triIdx.size());

        std::vector<uint32_t> tmp;
        tmp.assign(triIdx.begin(), triIdx.end());

        updateOrRecreate(fc,
                         m_subdivSharedTriIndexBuffer,
                         tmp,
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    // RT shader-readable triangle buffer (a,b,c,0)
    {
        const auto triIdx = subdiv->triangleIndices();

        if (!triIdx.empty() && (triIdx.size() % 3u) == 0u)
        {
            const uint32_t triCount = static_cast<uint32_t>(triIdx.size() / 3u);
            m_subdivRtTriCount      = triCount;

            std::vector<uint32_t> triIdx4;
            triIdx4.resize(size_t(triCount) * 4ull);

            for (uint32_t t = 0; t < triCount; ++t)
            {
                triIdx4[t * 4u + 0u] = triIdx[t * 3u + 0u];
                triIdx4[t * 4u + 1u] = triIdx[t * 3u + 1u];
                triIdx4[t * 4u + 2u] = triIdx[t * 3u + 2u];
                triIdx4[t * 4u + 3u] = 0u;
            }

            updateOrRecreate(fc,
                             m_subdivRtTriIndexBuffer,
                             triIdx4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
        else
        {
            m_subdivRtTriCount = 0;
        }
    }

    // ---------------------------------------------------------
    // NEW: RT per-triangle material IDs (uint32, indexed by primId)
    // ---------------------------------------------------------
    m_subdivRtMatIdCount = 0;

    if (m_subdivRtTriCount > 0)
    {
        const auto triMat = subdiv->triangleMaterialIds();
        if (triMat.size() == size_t(m_subdivRtTriCount))
        {
            std::vector<uint32_t> tmp;
            tmp.assign(triMat.begin(), triMat.end());

            m_subdivRtMatIdCount = static_cast<uint32_t>(tmp.size());

            updateOrRecreate(fc,
                             m_subdivRtMatIdBuffer,
                             tmp,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
    }

    // ---------------------------------------------------------
    // B) Subdiv solid representation (corner-expanded pos/nrm/uv/mat)
    // ---------------------------------------------------------
    {
        std::vector<glm::vec3> pos;
        std::vector<glm::vec3> nrm;
        std::vector<glm::vec2> uv;
        std::vector<uint32_t>  mat;

        buildSubdivCornerExpanded(*subdiv, pos, nrm, uv, mat);

        m_subdivPolyVertexCount = static_cast<uint32_t>(pos.size());

        updateOrRecreate(fc, m_subdivPolyVertBuffer, pos, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);
        updateOrRecreate(fc, m_subdivPolyNormBuffer, nrm, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);
        updateOrRecreate(fc, m_subdivPolyUvBuffer, uv, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);
        updateOrRecreate(fc, m_subdivPolyMatIdBuffer, mat, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);

        // RT per-corner normals (vec4 padded)
        m_subdivRtCornerNrmCount = 0;
        if (m_subdivRtTriCount > 0 && !nrm.empty())
        {
            const size_t expected = size_t(m_subdivRtTriCount) * 3ull;
            if (nrm.size() == expected)
            {
                std::vector<glm::vec4> nrm4(nrm.size());
                for (size_t i = 0; i < nrm.size(); ++i)
                    nrm4[i] = glm::vec4(nrm[i], 0.0f);

                m_subdivRtCornerNrmCount = static_cast<uint32_t>(nrm4.size());

                updateOrRecreate(fc,
                                 m_subdivRtCornerNrmBuffer,
                                 nrm4,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 kCapacity64KiB,
                                 /*deviceAddress*/ true);
            }
        }

        // RT per-corner UVs (vec4 padded)
        m_subdivRtCornerUvCount = 0;
        if (m_subdivRtTriCount > 0 && !uv.empty())
        {
            const size_t expected = size_t(m_subdivRtTriCount) * 3ull;
            if (uv.size() == expected)
            {
                std::vector<glm::vec4> uv4(uv.size());
                for (size_t i = 0; i < uv.size(); ++i)
                    uv4[i] = glm::vec4(uv[i], 0.0f, 0.0f);

                m_subdivRtCornerUvCount = static_cast<uint32_t>(uv4.size());

                updateOrRecreate(fc,
                                 m_subdivRtCornerUvBuffer,
                                 uv4,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 kCapacity64KiB,
                                 /*deviceAddress*/ true);
            }
        }
    }

    // ---------------------------------------------------------
    // C) Primary edges
    // ---------------------------------------------------------
    {
        const auto            primary = subdiv->primaryEdges();
        std::vector<uint32_t> lineIdx = flattenEdgePairs(primary);

        m_subdivPrimaryEdgeIndexCount = static_cast<uint32_t>(lineIdx.size());

        updateOrRecreate(fc,
                         m_subdivPrimaryEdgeIndexBuffer,
                         lineIdx,
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         kCapacity64KiB);
    }

    // Barriers:
    //  - subdiv solid/shared vertex streams are vertex-input reads
    //  - subdiv shared/primary edge indices are vertex-input reads
    //  - BLAS build reads shared vert + shared tri index
    //  - RT shaders read storage buffers (pos/tri/nrm/uv/mat)
    vkutil::barrierTransferToVertexAttributeRead(fc.cmd);
    vkutil::barrierTransferToIndexRead(fc.cmd);

    vkutil::barrierTransferToAsBuildRead(fc.cmd);
    vkutil::barrierTransferToRtShaderRead(fc.cmd);
}

// ============================================================================
// SUBDIV DEFORM UPDATE (level constant; topology constant)
// ============================================================================

void MeshGpuResources::updateSubdivDeform(const RenderFrameContext& fc, const SysMesh* sys, int level)
{
    if (!sys || !m_ctx || !fc.cmd)
        return;

    SubdivEvaluator* subdiv = m_owner ? m_owner->subdiv() : nullptr;
    if (!subdiv)
        return;

    if (subdiv->currentLevel() != level)
        subdiv->onLevelChanged(level);

    subdiv->evaluate();

    // A) shared verts update (+ RT pos)
    {
        const auto verts        = subdiv->vertices();
        m_subdivSharedVertCount = static_cast<uint32_t>(verts.size());

        std::vector<glm::vec3> tmp;
        tmp.assign(verts.begin(), verts.end());

        updateOrRecreate(fc,
                         m_subdivSharedVertBuffer,
                         tmp,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);

        m_subdivRtPosCount = m_subdivSharedVertCount;

        std::vector<glm::vec4> tmp4(tmp.size());
        for (size_t i = 0; i < tmp.size(); ++i)
            tmp4[i] = glm::vec4(tmp[i], 1.0f);

        updateOrRecreate(fc,
                         m_subdivRtPosBuffer,
                         tmp4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    // B) solid corner-expanded: update pos + nrm (UV/mat unchanged on deform)
    {
        std::vector<glm::vec3> pos;
        std::vector<glm::vec3> nrm;
        std::vector<glm::vec2> uv;
        std::vector<uint32_t>  mat;

        buildSubdivCornerExpanded(*subdiv, pos, nrm, uv, mat);

        m_subdivPolyVertexCount = static_cast<uint32_t>(pos.size());

        updateOrRecreate(fc, m_subdivPolyVertBuffer, pos, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);
        updateOrRecreate(fc, m_subdivPolyNormBuffer, nrm, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kCapacity64KiB);

        // RT per-corner normals update
        m_subdivRtCornerNrmCount = 0;
        if (m_subdivRtTriCount > 0 && !nrm.empty())
        {
            const size_t expected = size_t(m_subdivRtTriCount) * 3ull;
            if (nrm.size() == expected)
            {
                std::vector<glm::vec4> nrm4(nrm.size());
                for (size_t i = 0; i < nrm.size(); ++i)
                    nrm4[i] = glm::vec4(nrm[i], 0.0f);

                m_subdivRtCornerNrmCount = static_cast<uint32_t>(nrm4.size());

                updateOrRecreate(fc,
                                 m_subdivRtCornerNrmBuffer,
                                 nrm4,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 kCapacity64KiB,
                                 /*deviceAddress*/ true);
            }
        }
    }

    // Barriers: vertex input reads + BLAS build reads (shared verts) + RT shader reads
    vkutil::barrierTransferToVertexAttributeRead(fc.cmd);
    vkutil::barrierTransferToAsBuildRead(fc.cmd);
    vkutil::barrierTransferToRtShaderRead(fc.cmd);
}

void MeshGpuResources::updateSelectionBuffersSubdiv(const RenderFrameContext& fc, const SysMesh* sys, int level)
{
    if (!sys)
        return;

    SubdivEvaluator* subdiv = m_owner->subdiv();
    if (!subdiv)
        return;

    if (subdiv->currentLevel() != level)
        subdiv->onLevelChanged(level);

    // ---------------------------------------------------------
    // 1) Selected verts -> point indices into subdiv verts
    // ---------------------------------------------------------
    std::vector<uint32_t> outV;
    {
        const std::vector<uint32_t> selV = extractSelectedVertices(sys); // base vertex IDs (SysMesh indices)
        outV.reserve(selV.size());

        for (uint32_t baseVi : selV)
        {
            const int limitVi = subdiv->limitVert((int)baseVi);
            if (limitVi >= 0)
                outV.push_back((uint32_t)limitVi);
        }
    }

    // ---------------------------------------------------------
    // 2) Selected edges -> line-list indices into subdiv verts
    // ---------------------------------------------------------
    std::vector<uint32_t> outE;
    {
        const std::vector<uint32_t> selE = extractSelectedEdges(sys);
        // selE is line-list of base vertex IDs: (a,b,a,b,...)
        outE.reserve(selE.size() * 2);

        for (size_t i = 0; i + 1 < selE.size(); i += 2)
        {
            const int a = (int)selE[i + 0];
            const int b = (int)selE[i + 1];

            const std::vector<int> limitEdges = subdiv->limitEdges(IndexPair{a, b});
            for (int le : limitEdges)
            {
                const auto ev = subdiv->edge(le); // expects 2 verts
                if (ev.size() == 2)
                {
                    outE.push_back((uint32_t)ev[0]);
                    outE.push_back((uint32_t)ev[1]);
                }
            }
        }
    }

    // ---------------------------------------------------------
    // 3) Selected polys -> triangle indices into subdiv verts
    // ---------------------------------------------------------
    std::vector<uint32_t> outP;
    {
        const std::vector<int32_t>& selPolys = sys->selected_polys();

        for (int32_t basePid : selPolys)
        {
            std::vector<uint32_t> tri = subdiv->triangleIndicesForBasePoly((int)basePid);
            outP.insert(outP.end(), tri.begin(), tri.end());
        }
    }

    m_subdivSelVertIndexCount = (uint32_t)outV.size();
    m_subdivSelEdgeIndexCount = (uint32_t)outE.size();
    m_subdivSelPolyIndexCount = (uint32_t)outP.size();

    updateOrRecreate(fc, m_subdivSelVertIndexBuffer, outV, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kCapacity64KiB);

    updateOrRecreate(fc, m_subdivSelEdgeIndexBuffer, outE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kCapacity64KiB);

    updateOrRecreate(fc, m_subdivSelPolyIndexBuffer, outP, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kCapacity64KiB);
}
