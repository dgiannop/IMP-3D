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
void MeshGpuResources::updateOrRecreate(GpuBuffer&            buffer,
                                        const std::vector<T>& data,
                                        VkBufferUsageFlags    usage,
                                        VkDeviceSize          initialCapacity,
                                        bool                  deviceAddress)
{
    if (data.empty())
        return;

    const VkDeviceSize size = sizeof(T) * data.size();

    if (buffer.valid() && size <= buffer.size())
    {
        vkutil::updateDeviceLocalBuffer(*m_ctx, buffer, 0, size, data.data());
        return;
    }

    const VkDeviceSize capacity =
        buffer.valid()
            ? std::max(size, buffer.size() + buffer.size() / 2)
            : std::max(size, initialCapacity);

    VkBufferUsageFlags finalUsage = usage;
    if (deviceAddress)
        finalUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    buffer = vkutil::createDeviceLocalBuffer(*m_ctx,
                                             capacity,
                                             finalUsage,
                                             data.data(),
                                             size,
                                             deviceAddress);
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
// NEW FAST-PATH UPDATE LOGIC
// ============================================================================

void MeshGpuResources::update()
{
    const SysMesh* sys = m_owner ? m_owner->sysMesh() : nullptr;
    if (!sys || !m_ctx)
        return;

    constexpr bool kForceFullRebuild = false;
    if (kForceFullRebuild)
    {
        fullRebuild(sys);
        updateSelectionBuffers(sys);
        m_cachedSubdivLevel = 0;
        return;
    }

    const int  level        = m_owner->subdivisionLevel();
    const bool levelChanged = (level != m_cachedSubdivLevel);

    const bool topoChanged   = m_topologyMonitor.changed();
    const bool deformChanged = m_deformMonitor.changed();
    const bool selectChanged = m_selectionMonitor.changed();

    // If nothing changed AND level didn’t change, nothing to do.
    if (!topoChanged && !deformChanged && !selectChanged && !levelChanged)
        return;

    // ---------------------------------------------------------
    // Subdivision path (level > 0)
    // ---------------------------------------------------------
    if (level > 0)
    {
        // Topology/level change: rebuild subdiv buffers.
        if (topoChanged || levelChanged)
        {
            fullRebuildSubdiv(sys, level);

            // Selection indices depend on refiner + level mapping, so refresh on topo/level too.
            updateSelectionBuffersSubdiv(sys, level);

            m_cachedSubdivLevel = level;
            return;
        }

        // Deform-only: update subdiv positions/normals.
        if (deformChanged)
        {
            updateSubdivDeform(sys, level);
        }

        // Selection-only: update subdiv selection buffers.
        if (selectChanged)
        {
            updateSelectionBuffersSubdiv(sys, level);
        }

        m_cachedSubdivLevel = level;
        return;
    }

    // ---------------------------------------------------------
    // Coarse path (level == 0)
    // ---------------------------------------------------------

    // If just came from subdiv -> coarse
    if (levelChanged)
    {
        // We switched representation (subdiv -> coarse). Coarse geometry buffers must be rebuilt.
        // Otherwise we may still have subdiv buffers bound/filled, causing level 0 to appear empty.
        fullRebuild(sys);
        updateSelectionBuffers(sys);

        m_cachedSubdivLevel = 0;
        return;
    }

    m_cachedSubdivLevel = 0;

    // Selection-only -> update only coarse selection buffers
    if (selectChanged && !topoChanged && !deformChanged)
    {
        updateSelectionBuffers(sys);
        return;
    }

    // Deform-only -> update only positions + normals (and selection if needed)
    if (deformChanged && !topoChanged)
    {
        updateDeformBuffers(sys);

        if (selectChanged)
            updateSelectionBuffers(sys);

        return;
    }

    // Topology changed -> full coarse rebuild (and selection if needed)
    if (topoChanged)
    {
        fullRebuild(sys);

        if (selectChanged)
            updateSelectionBuffers(sys);

        return;
    }

    // If we got here, it was some combo already handled above.
}

// ============================================================================
// TOPOLOGY REBUILD
// ============================================================================

void MeshGpuResources::fullRebuild(const SysMesh* sys)
{
    // Extract full triangulated mesh (corner-expanded attributes for solid draw)
    const MeshData tri     = extractMeshData(sys);
    const auto     edgeIdx = extractMeshEdgeIndices(sys);

    m_polyVertexCount = static_cast<uint32_t>(tri.verts.size());
    m_edgeIndexCount  = static_cast<uint32_t>(edgeIdx.size());

    // ---- Vertex attributes ----
    //
    // These are CORNER-EXPANDED (3 verts per tri). No index buffer is used for solid draw.
    updateOrRecreate(m_polyVertBuffer,
                     tri.verts,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    updateOrRecreate(m_polyNormBuffer,
                     tri.norms,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    updateOrRecreate(m_polyUvBuffer,
                     tri.uvPos,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    updateOrRecreate(m_polyMatIdBuffer,
                     tri.matIds,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // ---- Unique vertex buffer (shared by SysMesh vertex slot) ----
    //
    // Used for:
    //  - edge rendering (edgeIndexBuffer indices into this)
    //  - selection overlays (sel*IndexBuffer indices into this)
    //  - coarse ray tracing (shared positions, indexed by coarseTriIndexBuffer)
    const uint32_t slotCount = sys->vert_buffer_size();
    m_uniqueVertCount        = slotCount;

    std::vector<glm::vec3> uniqueVerts(slotCount, glm::vec3{0.0f});
    for (uint32_t vi = 0; vi < slotCount; ++vi)
        if (sys->vert_valid(static_cast<int32_t>(vi)))
            uniqueVerts[vi] = sys->vert_position(static_cast<int32_t>(vi));

    updateOrRecreate(m_uniqueVertBuffer,
                     uniqueVerts,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // ---- Edge index buffer ----
    updateOrRecreate(m_edgeIndexBuffer,
                     edgeIdx,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    // ---- Coarse RT triangle index buffer ----
    //
    // Indices into uniqueVertBuffer (shared slot positions).
    const std::vector<uint32_t> triIdx = extractMeshTriIndices(sys);
    m_coarseTriIndexCount              = static_cast<uint32_t>(triIdx.size());

    updateOrRecreate(m_coarseTriIndexBuffer,
                     triIdx,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // ---- Coarse RT shader-readable triangle buffer (padded uvec4) ----
    m_coarseRtTriCount = 0;

    if (!triIdx.empty() && (triIdx.size() % 3u) == 0u)
    {
        const uint32_t triCount = static_cast<uint32_t>(triIdx.size() / 3u);
        m_coarseRtTriCount      = triCount;

        std::vector<uint32_t> triIdx4;
        triIdx4.resize(size_t(triCount) * 4ull);

        for (uint32_t t = 0; t < triCount; ++t)
        {
            const uint32_t a = triIdx[t * 3u + 0u];
            const uint32_t b = triIdx[t * 3u + 1u];
            const uint32_t c = triIdx[t * 3u + 2u];

            triIdx4[t * 4u + 0u] = a;
            triIdx4[t * 4u + 1u] = b;
            triIdx4[t * 4u + 2u] = c;
            triIdx4[t * 4u + 3u] = 0u;
        }

        updateOrRecreate(m_coarseRtTriIndexBuffer,
                         triIdx4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    // ---- Coarse RT position buffer (padded vec4) ----
    m_coarseRtPosCount = slotCount;

    std::vector<glm::vec4> uniqueVerts4;
    uniqueVerts4.resize(slotCount, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});

    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
        {
            const glm::vec3 p = sys->vert_position(static_cast<int32_t>(vi));
            uniqueVerts4[vi]  = glm::vec4(p, 1.0f);
        }
    }

    updateOrRecreate(m_coarseRtPosBuffer,
                     uniqueVerts4,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // ---- Coarse RT CORNER normal buffer (padded vec4) ----
    //
    // 3 normals per RT primitive, in the same triangle order as triIdx.
    // corner = primId*3 + c
    m_coarseRtCornerNrmCount = 0;

    if (m_coarseRtTriCount > 0)
    {
        // Safety: keep us honest about ordering assumptions.
        if (tri.norms.size() == triIdx.size())
        {
            std::vector<glm::vec4> nrm4;
            nrm4.resize(tri.norms.size());

            for (size_t i = 0; i < tri.norms.size(); ++i)
                nrm4[i] = glm::vec4(tri.norms[i], 0.0f);

            m_coarseRtCornerNrmCount = static_cast<uint32_t>(nrm4.size());

            updateOrRecreate(m_coarseRtCornerNrmBuffer,
                             nrm4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
        else
        {
            // Mismatch means extractMeshData/extractMeshTriIndices diverged.
            // Leave count 0 so RT shading can't accidentally read stale data.
            m_coarseRtCornerNrmCount = 0;
        }
    }

    // ---- Coarse RT CORNER uv buffer (padded vec4) ----
    //
    // 3 uvs per RT primitive, in the same triangle order as triIdx.
    // corner = primId*3 + c
    m_coarseRtCornerUvCount = 0;

    if (m_coarseRtTriCount > 0)
    {
        if (tri.uvPos.size() == triIdx.size())
        {
            std::vector<glm::vec4> uv4;
            uv4.resize(tri.uvPos.size());

            for (size_t i = 0; i < tri.uvPos.size(); ++i)
                uv4[i] = glm::vec4(tri.uvPos[i], 0.0f, 0.0f);

            m_coarseRtCornerUvCount = static_cast<uint32_t>(uv4.size());

            updateOrRecreate(m_coarseRtCornerUvBuffer,
                             uv4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
        else
        {
            // Mismatch means extractMeshData/extractMeshTriIndices diverged.
            m_coarseRtCornerUvCount = 0;
        }
    }
}

// ============================================================================
// DEFORM update — positions and normals only
// ============================================================================

void MeshGpuResources::updateDeformBuffers(const SysMesh* sys)
{
    if (!sys)
        return;

    // ---------------------------------------------------------
    // 1) Unique slot verts (for edge/selection rendering + coarse RT)
    // ---------------------------------------------------------
    const uint32_t slotCount = sys->vert_buffer_size();
    m_uniqueVertCount        = slotCount;

    std::vector<glm::vec3> uniqueVerts(slotCount, glm::vec3{0.0f});

    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
            uniqueVerts[vi] = sys->vert_position(static_cast<int32_t>(vi));
    }

    updateOrRecreate(m_uniqueVertBuffer,
                     uniqueVerts,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);

    // ---------------------------------------------------------
    // 2) Triangle-expanded positions (for solid triangle draw)
    // ---------------------------------------------------------
    std::vector<glm::vec3> triVerts = extractTriPositionsOnly(sys);
    m_polyVertexCount               = static_cast<uint32_t>(triVerts.size());

    updateOrRecreate(m_polyVertBuffer,
                     triVerts,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     kCapacity64KiB);

    // ---------------------------------------------------------
    // 3) Normals (if you want correct lighting during move)
    // ---------------------------------------------------------
    std::vector<glm::vec3> norms = extractPolyNormasOnly(sys);
    if (!norms.empty())
    {
        updateOrRecreate(m_polyNormBuffer,
                         norms,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);
    }

    // NOTE:
    // coarseTriIndexBuffer does not change on deform. It only changes on topology edits.

    m_coarseRtPosCount = slotCount;

    std::vector<glm::vec4> uniqueVerts4;
    uniqueVerts4.resize(slotCount, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});

    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(static_cast<int32_t>(vi)))
        {
            const glm::vec3 p = sys->vert_position(static_cast<int32_t>(vi));
            uniqueVerts4[vi]  = glm::vec4(p, 1.0f);
        }
    }

    updateOrRecreate(m_coarseRtPosBuffer,
                     uniqueVerts4,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     kCapacity64KiB,
                     /*deviceAddress*/ true);
}

// ============================================================================
// SELECTION update (coarse)
// ============================================================================

void MeshGpuResources::updateSelectionBuffers(const SysMesh* sys)
{
    auto selV = extractSelectedVertices(sys);
    auto selE = extractSelectedEdges(sys);
    auto selP = extractSelectedPolyTriangles(sys);

    m_selVertIndexCount = static_cast<uint32_t>(selV.size());
    m_selEdgeIndexCount = static_cast<uint32_t>(selE.size());
    m_selPolyIndexCount = static_cast<uint32_t>(selP.size());

    updateOrRecreate(m_selVertIndexBuffer,
                     selV,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    updateOrRecreate(m_selEdgeIndexBuffer,
                     selE,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    updateOrRecreate(m_selPolyIndexBuffer,
                     selP,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
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
// Subdiv rebuild
// ============================================================================

void MeshGpuResources::fullRebuildSubdiv(const SysMesh* sys, int level)
{
    if (!sys)
        return;

    SubdivEvaluator* subdiv = m_owner->subdiv();
    if (!subdiv)
        return;

    // Topology rebuild + refine to level + build per-level products + evaluate
    subdiv->onTopologyChanged(const_cast<SysMesh*>(sys), level);

    // ---------------------------------------------------------
    // A) Subdiv shared representation (aux/debug/compute)
    // ---------------------------------------------------------
    {
        const auto verts        = subdiv->vertices();
        m_subdivSharedVertCount = static_cast<uint32_t>(verts.size());

        std::vector<glm::vec3> tmp;
        tmp.assign(verts.begin(), verts.end());

        updateOrRecreate(m_subdivSharedVertBuffer,
                         tmp,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);

        m_subdivRtPosCount = m_subdivSharedVertCount;

        std::vector<glm::vec4> tmp4;
        tmp4.resize(tmp.size());

        for (size_t i = 0; i < tmp.size(); ++i)
            tmp4[i] = glm::vec4(tmp[i], 1.0f);

        updateOrRecreate(m_subdivRtPosBuffer,
                         tmp4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    {
        const auto triIdx           = subdiv->triangleIndices();
        m_subdivSharedTriIndexCount = static_cast<uint32_t>(triIdx.size());

        std::vector<uint32_t> tmp;
        tmp.assign(triIdx.begin(), triIdx.end());

        updateOrRecreate(m_subdivSharedTriIndexBuffer,
                         tmp,
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    // ---- Subdiv RT shader-readable triangle buffer (padded uvec4) ----
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
                const uint32_t a = triIdx[t * 3u + 0u];
                const uint32_t b = triIdx[t * 3u + 1u];
                const uint32_t c = triIdx[t * 3u + 2u];

                triIdx4[t * 4u + 0u] = a;
                triIdx4[t * 4u + 1u] = b;
                triIdx4[t * 4u + 2u] = c;
                triIdx4[t * 4u + 3u] = 0u;
            }

            updateOrRecreate(m_subdivRtTriIndexBuffer,
                             triIdx4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             kCapacity64KiB,
                             /*deviceAddress*/ true);
        }
        else
        {
            m_subdivRtTriCount = 0;
        }
    }

    // ---------------------------------------------------------
    // B) Subdiv solid representation (SysMesh semantics)
    //     corner-expanded pos/nrm/uv/mat, no indices
    // ---------------------------------------------------------
    {
        std::vector<glm::vec3> pos;
        std::vector<glm::vec3> nrm;
        std::vector<glm::vec2> uv;
        std::vector<uint32_t>  mat;

        buildSubdivCornerExpanded(*subdiv, pos, nrm, uv, mat);

        m_subdivPolyVertexCount = static_cast<uint32_t>(pos.size());

        updateOrRecreate(m_subdivPolyVertBuffer,
                         pos,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);

        updateOrRecreate(m_subdivPolyNormBuffer,
                         nrm,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);

        updateOrRecreate(m_subdivPolyUvBuffer,
                         uv,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);

        updateOrRecreate(m_subdivPolyMatIdBuffer,
                         mat,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);

        // ---- Subdiv RT CORNER normal buffer (padded vec4) ----
        //
        // 3 normals per RT primitive, in RT primitive order:
        // corner = primId*3 + c
        m_subdivRtCornerNrmCount = 0;

        if (m_subdivRtTriCount > 0 && !nrm.empty())
        {
            const size_t expected = size_t(m_subdivRtTriCount) * 3ull;

            if (nrm.size() == expected)
            {
                std::vector<glm::vec4> nrm4;
                nrm4.resize(nrm.size());

                for (size_t i = 0; i < nrm.size(); ++i)
                    nrm4[i] = glm::vec4(nrm[i], 0.0f);

                m_subdivRtCornerNrmCount = static_cast<uint32_t>(nrm4.size());

                updateOrRecreate(m_subdivRtCornerNrmBuffer,
                                 nrm4,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 kCapacity64KiB,
                                 /*deviceAddress*/ true);
            }
        }

        // ---- Subdiv RT CORNER uv buffer (padded vec4) ----
        //
        // 3 uvs per RT primitive, in RT primitive order:
        // corner = primId*3 + c
        m_subdivRtCornerUvCount = 0;

        if (m_subdivRtTriCount > 0 && !uv.empty())
        {
            const size_t expected = size_t(m_subdivRtTriCount) * 3ull;

            if (uv.size() == expected)
            {
                std::vector<glm::vec4> uv4;
                uv4.resize(uv.size());

                for (size_t i = 0; i < uv.size(); ++i)
                    uv4[i] = glm::vec4(uv[i], 0.0f, 0.0f);

                m_subdivRtCornerUvCount = static_cast<uint32_t>(uv4.size());

                updateOrRecreate(m_subdivRtCornerUvBuffer,
                                 uv4,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 kCapacity64KiB,
                                 /*deviceAddress*/ true);
            }
        }
    }

    // ---------------------------------------------------------
    // C) Primary edges (coarse-derived)
    // ---------------------------------------------------------
    {
        const auto            primary = subdiv->primaryEdges();
        std::vector<uint32_t> lineIdx = flattenEdgePairs(primary);

        m_subdivPrimaryEdgeIndexCount = static_cast<uint32_t>(lineIdx.size());

        updateOrRecreate(m_subdivPrimaryEdgeIndexBuffer,
                         lineIdx,
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);
    }
}

void MeshGpuResources::updateSubdivDeform(const SysMesh* sys, int level)
{
    if (!sys)
        return;

    SubdivEvaluator* subdiv = m_owner->subdiv();
    if (!subdiv)
        return;

    if (subdiv->currentLevel() != level)
        subdiv->onLevelChanged(level);

    subdiv->evaluate();

    // A) shared verts update
    {
        const auto verts        = subdiv->vertices();
        m_subdivSharedVertCount = static_cast<uint32_t>(verts.size());

        std::vector<glm::vec3> tmp;
        tmp.assign(verts.begin(), verts.end());

        updateOrRecreate(m_subdivSharedVertBuffer,
                         tmp,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);

        // Keep RT pos buffer in sync too
        m_subdivRtPosCount = m_subdivSharedVertCount;

        std::vector<glm::vec4> tmp4;
        tmp4.resize(tmp.size());

        for (size_t i = 0; i < tmp.size(); ++i)
            tmp4[i] = glm::vec4(tmp[i], 1.0f);

        updateOrRecreate(m_subdivRtPosBuffer,
                         tmp4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB,
                         /*deviceAddress*/ true);
    }

    // B) solid corner-expanded: update pos + nrm
    // (UV/mat don't change on deform; you can skip them here)
    {
        std::vector<glm::vec3> pos;
        std::vector<glm::vec3> nrm;
        std::vector<glm::vec2> uv;
        std::vector<uint32_t>  mat;

        buildSubdivCornerExpanded(*subdiv, pos, nrm, uv, mat);

        m_subdivPolyVertexCount = static_cast<uint32_t>(pos.size());

        updateOrRecreate(m_subdivPolyVertBuffer,
                         pos,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);

        updateOrRecreate(m_subdivPolyNormBuffer,
                         nrm,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         kCapacity64KiB);

        // ---- Subdiv RT CORNER normal buffer update ----
        m_subdivRtCornerNrmCount = 0;

        if (m_subdivRtTriCount > 0 && !nrm.empty())
        {
            const size_t expected = size_t(m_subdivRtTriCount) * 3ull;

            if (nrm.size() == expected)
            {
                std::vector<glm::vec4> nrm4;
                nrm4.resize(nrm.size());

                for (size_t i = 0; i < nrm.size(); ++i)
                    nrm4[i] = glm::vec4(nrm[i], 0.0f);

                m_subdivRtCornerNrmCount = static_cast<uint32_t>(nrm4.size());

                updateOrRecreate(m_subdivRtCornerNrmBuffer,
                                 nrm4,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 kCapacity64KiB,
                                 /*deviceAddress*/ true);
            }
        }
    }

    // NOTE:
    // subdivSharedTriIndexBuffer does not change on deform.
}

void MeshGpuResources::updateSelectionBuffersSubdiv(const SysMesh* sys, int level)
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

    updateOrRecreate(m_subdivSelVertIndexBuffer,
                     outV,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     kCapacity64KiB);

    updateOrRecreate(m_subdivSelEdgeIndexBuffer,
                     outE,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     kCapacity64KiB);

    updateOrRecreate(m_subdivSelPolyIndexBuffer,
                     outP,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     kCapacity64KiB);
}
