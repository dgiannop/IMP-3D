// SubdivEvaluator.cpp
#include "SubdivEvaluator.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <glm/gtx/norm.hpp>
#include <map>

namespace
{
    int prefixFaces(const OpenSubdiv::Far::TopologyRefiner* ref, int level) noexcept
    {
        int off = 0;
        for (int L = 0; L < level; ++L)
            off += ref->GetLevel(L).GetNumFaces();
        return off;
    }

    int prefixVerts(const OpenSubdiv::Far::TopologyRefiner* ref, int level) noexcept
    {
        int off = 0;
        for (int L = 0; L < level; ++L)
            off += ref->GetLevel(L).GetNumVertices();
        return off;
    }

    int prefixFVars(const OpenSubdiv::Far::TopologyRefiner* ref, int level, int channel) noexcept
    {
        int off = 0;
        for (int L = 0; L < level; ++L)
            off += ref->GetLevel(L).GetNumFVarValues(channel);
        return off;
    }
} // namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void SubdivEvaluator::onTopologyChanged(SysMesh* mesh, int level)
{
    if (!mesh)
        return;

    m_sysMesh      = mesh;
    m_levelCurrent = std::max(0, level);

    // Guard: loaders/tools sometimes trigger this while the mesh is still empty / rebuilding.
    // Never feed OpenSubdiv an empty descriptor.
    if (mesh->num_verts() <= 0 || mesh->num_polys() <= 0)
    {
        m_sdsMesh.clear();

        m_verts.clear();
        m_norms.clear();
        m_tris.clear();
        m_triUV.clear();
        m_triMat.clear();
        m_edges.clear();
        m_uvs.clear();

        m_faceUniformAll.clear();
        m_fvarValuesL0.clear();
        m_fvarAll.clear();

        return;
    }

    buildDescriptorFromMesh(mesh);

    // buildDescriptorFromMesh may decide to clear/skip on invalid topology
    if (!m_sdsMesh.valid())
        return;

    m_sdsMesh.refine(m_levelCurrent);

    // Interpolate face-uniform materials across all refined levels.
    m_sdsMesh.interpolate_face_uniform(m_faceUniformAll);

    // Interpolate face-varying UVs across all refined levels.
    m_fvarAll = m_fvarValuesL0;
    m_sdsMesh.interpolate_face_varying(m_fvarAll, 0);

    rebuildPerLevelProducts(m_levelCurrent);
    evaluate();
}

void SubdivEvaluator::onLevelChanged(int level)
{
    level = std::max(0, level);
    if (level == m_levelCurrent)
        return;

    m_levelCurrent = level;

    ensureRefinedTo(level);

    // Refresh interpolated arrays (now maybe longer if we extended refinement).
    m_sdsMesh.interpolate_face_uniform(m_faceUniformAll);

    m_fvarAll = m_fvarValuesL0;
    m_sdsMesh.interpolate_face_varying(m_fvarAll, 0);

    rebuildPerLevelProducts(level);
    evaluate();
}

void SubdivEvaluator::evaluate()
{
    auto* ref = m_sdsMesh.refiner();
    if (!ref || !m_sysMesh)
        return;

    const int lvl = std::clamp(m_levelCurrent, 0, ref->GetMaxLevel());

    // Gather coarse positions in dense order
    const int              nCoarse = ref->GetLevel(0).GetNumVertices();
    std::vector<glm::vec3> prim(static_cast<size_t>(nCoarse), glm::vec3(0.0f));

    for (int i = 0; i < nCoarse; ++i)
    {
        const int baseVi = (i < (int)m_vremap.size()) ? m_vremap[(size_t)i] : -1;
        if (baseVi >= 0)
            prim[(size_t)i] = m_sysMesh->vert_position(baseVi);
    }

    // Interpolate across all built levels (contiguous layout)
    m_sdsMesh.interpolate(prim);

    const int off   = prefixVerts(ref, lvl);
    const int count = ref->GetLevel(lvl).GetNumVertices();

    m_verts.resize((size_t)count);
    for (int i = 0; i < count; ++i)
        m_verts[(size_t)i] = prim[(size_t)(off + i)];

    recomputeNormalsFromTris();
}

void SubdivEvaluator::recomputeNormalsFromTris()
{
    const size_t vCount = m_verts.size();
    m_norms.assign(vCount, glm::vec3(0.0f));

    const size_t triCount = m_tris.size() / 3;
    for (size_t t = 0; t < triCount; ++t)
    {
        const uint32_t i0 = m_tris[3 * t + 0];
        const uint32_t i1 = m_tris[3 * t + 1];
        const uint32_t i2 = m_tris[3 * t + 2];

        if (i0 >= vCount || i1 >= vCount || i2 >= vCount)
            continue;

        const glm::vec3& v0 = m_verts[(size_t)i0];
        const glm::vec3& v1 = m_verts[(size_t)i1];
        const glm::vec3& v2 = m_verts[(size_t)i2];

        const glm::vec3 fn = glm::cross(v1 - v0, v2 - v0); // area-weighted

        m_norms[(size_t)i0] += fn;
        m_norms[(size_t)i1] += fn;
        m_norms[(size_t)i2] += fn;
    }

    for (glm::vec3& n : m_norms)
    {
        const float len2 = glm::length2(n);
        if (len2 > 1e-20f)
            n *= (1.0f / std::sqrt(len2));
        else
            n = glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

// -----------------------------------------------------------------------------
// Descriptor build (SysMesh -> OSD descriptor)
// -----------------------------------------------------------------------------

void SubdivEvaluator::buildDescriptorFromMesh(SysMesh* mesh)
{
    assert(mesh);

    // Clear everything
    m_vremap.clear();
    m_vremapInv.clear();
    m_premap.clear();
    m_premapInv.clear();

    m_tremap.clear();
    m_tremapInv.clear();

    m_numVertsPerFace.clear();
    m_vertIndicesPerCorner.clear();
    m_fvarIndicesPerCorner.clear();

    m_fvarValuesL0.clear();
    m_faceUniformAll.clear();
    m_fvarAll.clear();

    // Reset persistent channel storage
    m_uvChannel = {};

    // --- dense vertex map ---
    m_vremap.reserve(mesh->num_verts());
    for (int32_t vi : mesh->all_verts())
    {
        m_vremapInv.emplace(vi, (int)m_vremap.size());
        m_vremap.push_back(vi);
    }

    // --- dense poly map (only for helper funcs, not required by descriptor) ---
    m_premap.reserve(mesh->num_polys());
    for (int32_t pid : mesh->all_polys())
    {
        m_premapInv.emplace(pid, (int)m_premap.size());
        m_premap.push_back(pid);
    }

    // Build faces/corners arrays for the descriptor.
    // IMPORTANT: order of faces must match the materials we seed into faceUniformL0.
    std::vector<int> faceUniformL0 = {};

    const size_t approxCorners = (size_t)mesh->num_polys() * 4ull;
    m_vertIndicesPerCorner.reserve(approxCorners);
    m_fvarIndicesPerCorner.reserve(approxCorners);
    faceUniformL0.reserve(mesh->num_polys());
    m_numVertsPerFace.reserve(mesh->num_polys());

    const int32_t uvMapId = 1;
    const bool    hasUV   = (mesh->map_find(uvMapId) != -1);

    for (int32_t pid : mesh->all_polys())
    {
        const SysPolyVerts& pv = mesh->poly_verts(pid);
        const int           n  = (int)pv.size();
        if (n < 3)
            continue;

        m_numVertsPerFace.push_back(n);

        int mat = (int)mesh->poly_material(pid);
        if (mat < 0)
            mat = 0;
        faceUniformL0.push_back(mat);

        SysPolyVerts mv = {};
        if (hasUV && mesh->map_poly_valid(uvMapId, pid))
            mv = mesh->map_poly_verts(uvMapId, pid);

        for (int c = 0; c < n; ++c)
        {
            // corner vertex -> dense vertex
            const int32_t baseVi = pv[c];
            const auto    itV    = m_vremapInv.find(baseVi);
            assert(itV != m_vremapInv.end());
            m_vertIndicesPerCorner.push_back(itV->second);

            // corner UV -> dense fvar (map vert ID based)
            int32_t baseMv = 0;
            if (hasUV && c < (int)mv.size())
                baseMv = mv[c];

            auto itT = m_tremapInv.find(baseMv);
            int  denseFv;
            if (itT == m_tremapInv.end())
            {
                denseFv = (int)m_tremap.size();
                m_tremapInv.emplace(baseMv, denseFv);
                m_tremap.push_back(baseMv);
            }
            else
            {
                denseFv = itT->second;
            }

            m_fvarIndicesPerCorner.push_back(denseFv);
        }
    }

    // Seed face-uniform vector for L0 (size == num coarse faces we actually emitted)
    m_faceUniformAll = faceUniformL0;

    // Seed L0 fvar values using map vert positions
    m_fvarValuesL0.resize(m_tremap.size(), glm::vec2(0.0f));

    if (hasUV)
    {
        for (size_t i = 0; i < m_tremap.size(); ++i)
        {
            const int32_t baseMv = m_tremap[i];
            const float*  p      = mesh->map_vert_position(uvMapId, baseMv);
            if (p)
                m_fvarValuesL0[i] = glm::vec2(p[0], p[1]);
        }
    }

    // Build descriptor (pointers must remain valid during Create() call)
    OpenSubdiv::Far::TopologyDescriptor desc = {};
    desc.numVertices                         = (int)m_vremap.size();
    desc.numFaces                            = (int)m_numVertsPerFace.size();
    desc.numVertsPerFace                     = m_numVertsPerFace.data();
    desc.vertIndicesPerFace                  = m_vertIndicesPerCorner.data();

    // IMPORTANT: uv channel storage must outlive the Create() call
    desc.numFVarChannels = 0;
    desc.fvarChannels    = nullptr;

    if (!m_tremap.empty())
    {
        m_uvChannel              = {};
        m_uvChannel.numValues    = (int)m_tremap.size();
        m_uvChannel.valueIndices = m_fvarIndicesPerCorner.data();

        desc.numFVarChannels = 1;
        desc.fvarChannels    = &m_uvChannel;
    }

    // (Re)create refiner
    m_sdsMesh.clear();
    m_sdsMesh.create(desc);

    // Clear per-level outputs
    m_verts.clear();
    m_norms.clear();
    m_tris.clear();
    m_triUV.clear();
    m_triMat.clear();
    m_edges.clear();
    m_uvs.clear();
}

// -----------------------------------------------------------------------------
// Per-level products
// -----------------------------------------------------------------------------

void SubdivEvaluator::ensureRefinedTo(int level)
{
    auto* ref = m_sdsMesh.refiner();
    if (!ref)
        return;

    const int builtMax = ref->GetMaxLevel();
    if (level <= builtMax)
        return;

    m_sdsMesh.refine(level);
}

void SubdivEvaluator::sliceUVsForLevel(int level)
{
    auto* ref = m_sdsMesh.refiner();
    if (!ref)
        return;

    const int lvl = std::clamp(level, 0, ref->GetMaxLevel());

    const int fvarCount = ref->GetLevel(lvl).GetNumFVarValues(0);
    const int fvarOff   = prefixFVars(ref, lvl, 0);

    m_uvs.resize((size_t)fvarCount);
    for (int i = 0; i < fvarCount; ++i)
        m_uvs[(size_t)i] = m_fvarAll[(size_t)(fvarOff + i)];
}

void SubdivEvaluator::rebuildPerLevelProducts(int level)
{
    auto* ref = m_sdsMesh.refiner();
    if (!ref)
        return;

    const int   lvl = std::clamp(level, 0, ref->GetMaxLevel());
    const auto& L   = ref->GetLevel(lvl);

    // UVs for this level
    if (!m_tremap.empty())
        sliceUVsForLevel(lvl);
    else
        m_uvs.clear();

    // Triangles + UV indices + materials
    m_tris.clear();
    m_triUV.clear();
    m_triMat.clear();

    const int faceOff   = prefixFaces(ref, lvl);
    const int faceCount = L.GetNumFaces();

    m_tris.reserve((size_t)faceCount * 6ull);
    m_triUV.reserve(m_tris.capacity());
    m_triMat.reserve((size_t)faceCount * 2ull);

    const bool hasFVar = (L.GetNumFVarChannels() > 0);

    for (int f = 0; f < faceCount; ++f)
    {
        const auto verts = L.GetFaceVertices(f);
        const int  N     = (int)verts.size();
        if (N < 3)
            continue;

        SdsMesh::IndexArray fvars = {};
        if (hasFVar)
            fvars = L.GetFaceFVarValues(f, 0);

        const int mat =
            (faceOff + f < (int)m_faceUniformAll.size())
                ? m_faceUniformAll[(size_t)(faceOff + f)]
                : 0;

        for (int j = 1; j + 1 < N; ++j)
        {
            const uint32_t i0 = (uint32_t)verts[0];
            const uint32_t i1 = (uint32_t)verts[j];
            const uint32_t i2 = (uint32_t)verts[j + 1];

            m_tris.push_back(i0);
            m_tris.push_back(i1);
            m_tris.push_back(i2);

            if (hasFVar && fvars.size() == verts.size())
            {
                const uint32_t u0 = (uint32_t)fvars[0];
                const uint32_t u1 = (uint32_t)fvars[j];
                const uint32_t u2 = (uint32_t)fvars[j + 1];

                m_triUV.push_back(u0);
                m_triUV.push_back(u1);
                m_triUV.push_back(u2);
            }
            else
            {
                m_triUV.push_back(0);
                m_triUV.push_back(0);
                m_triUV.push_back(0);
            }

            m_triMat.push_back((uint32_t)mat);
        }
    }

    // Edge list (level-local vertices)
    m_edges.clear();
    m_edges.reserve((size_t)L.GetNumEdges());

    for (int e = 0, eEnd = L.GetNumEdges(); e < eEnd; ++e)
    {
        auto ev = L.GetEdgeVertices(e);
        if (ev.size() == 2)
            m_edges.emplace_back((uint32_t)ev[0], (uint32_t)ev[1]);
    }
}

// -----------------------------------------------------------------------------
// Helpers: base -> limit mapping
// -----------------------------------------------------------------------------

int SubdivEvaluator::limitVert(int baseVertIndex) const
{
    const auto it = m_vremapInv.find(baseVertIndex);
    if (it == m_vremapInv.end())
        return -1;

    return m_sdsMesh.limit_vert(it->second);
}

std::vector<int> SubdivEvaluator::limitEdges(IndexPair baseEdge) const
{
    const auto itA = m_vremapInv.find(baseEdge.first);
    const auto itB = m_vremapInv.find(baseEdge.second);

    if (itA == m_vremapInv.end() || itB == m_vremapInv.end())
        return {};

    return m_sdsMesh.limit_edges({itA->second, itB->second});
}

uint32_t SubdivEvaluator::faceMaterialId(int face) const noexcept
{
    const auto* ref = m_sdsMesh.refiner();
    if (!ref)
        return 0;

    const int lvl = std::clamp(m_levelCurrent, 0, ref->GetMaxLevel());

    const int faceCount = ref->GetLevel(lvl).GetNumFaces();
    if (face < 0 || face >= faceCount)
        return 0;

    const int faceOff = prefixFaces(ref, lvl);
    const int idx     = faceOff + face;

    if (idx < 0 || idx >= (int)m_faceUniformAll.size())
        return 0;

    const int mat = m_faceUniformAll[(size_t)idx];
    return (mat >= 0) ? (uint32_t)mat : 0u;
}

// -----------------------------------------------------------------------------
// Optional utilities (kept from your previous version)
// -----------------------------------------------------------------------------

std::vector<std::pair<int, int>>
SubdivEvaluator::refinedOutlineEdgesForPolys(const std::vector<int>& basePolys) const
{
    if (basePolys.empty() || !m_sdsMesh.refiner())
        return {};

    const auto* ref = m_sdsMesh.refiner();
    const int   lvl = std::clamp(m_levelCurrent, 0, ref->GetMaxLevel());
    const auto& L   = ref->GetLevel(lvl);

    auto expandFaceToLevel = [&](int coarseFace) {
        std::vector<int> faces;
        faces.push_back(coarseFace);
        for (int l = 1; l <= lvl; ++l)
        {
            std::vector<int> next;
            next.reserve(faces.size() * 4);
            for (int f : faces)
            {
                auto kids = ref->GetLevel(l - 1).GetFaceChildFaces(f);
                next.insert(next.end(), kids.begin(), kids.end());
            }
            faces.swap(next);
        }
        return faces;
    };

    std::map<std::pair<int, int>, int> edgeCount;

    for (int basePoly : basePolys)
    {
        const auto it = m_premapInv.find(basePoly);
        if (it == m_premapInv.end())
            continue;

        const int  densePoly    = it->second;
        const auto refinedFaces = expandFaceToLevel(densePoly);

        for (int f : refinedFaces)
        {
            auto      verts = L.GetFaceVertices(f);
            const int N     = (int)verts.size();
            for (int i = 0; i < N; ++i)
            {
                const int  a = verts[i];
                const int  b = verts[(i + 1) % N];
                const auto e = std::minmax(a, b);
                edgeCount[e] += 1;
            }
        }
    }

    std::vector<std::pair<int, int>> result;
    result.reserve(edgeCount.size());
    for (const auto& [e, count] : edgeCount)
        if (count == 1)
            result.push_back(e);
    return result;
}

std::vector<uint32_t>
SubdivEvaluator::triangleIndicesForBasePoly(int basePoly) const
{
    std::vector<uint32_t> out;
    const auto*           ref = m_sdsMesh.refiner();
    if (!ref)
        return out;

    const int  lvl = std::clamp(m_levelCurrent, 0, ref->GetMaxLevel());
    const auto it  = m_premapInv.find(basePoly);
    if (it == m_premapInv.end())
        return out;

    const int densePoly = it->second;

    std::vector<int> faces{densePoly};
    for (int l = 1; l <= lvl; ++l)
    {
        std::vector<int> next;
        next.reserve(faces.size() * 4);
        for (int f : faces)
        {
            auto kids = ref->GetLevel(l - 1).GetFaceChildFaces(f);
            next.insert(next.end(), kids.begin(), kids.end());
        }
        faces.swap(next);
    }

    const auto& L = ref->GetLevel(lvl);
    out.reserve(faces.size() * 6);

    for (int f : faces)
    {
        auto v = L.GetFaceVertices(f);
        if (v.size() < 3)
            continue;

        // Fan triangulation
        for (int j = 1; j + 1 < v.size(); ++j)
        {
            out.push_back((uint32_t)v[0]);
            out.push_back((uint32_t)v[j]);
            out.push_back((uint32_t)v[j + 1]);
        }
    }

    return out;
}

std::vector<std::pair<uint32_t, uint32_t>>
SubdivEvaluator::primaryEdges() const
{
    std::vector<std::pair<uint32_t, uint32_t>> result;

    if (!m_sdsMesh.valid())
        return result;

    const auto* ref = m_sdsMesh.refiner();
    const int   lvl = std::clamp(m_levelCurrent, 0, ref->GetMaxLevel());
    const auto& L   = ref->GetLevel(lvl);

    auto expandEdgeToLevel = [&](int coarseEdge) {
        std::vector<int> edges{coarseEdge};
        for (int l = 1; l <= lvl; ++l)
        {
            std::vector<int> next;
            for (int e : edges)
            {
                auto kids = ref->GetLevel(l - 1).GetEdgeChildEdges(e);
                next.insert(next.end(), kids.begin(), kids.end());
            }
            edges.swap(next);
        }
        return edges;
    };

    result.reserve((size_t)ref->GetLevel(0).GetNumEdges());

    for (int eid = 0, eEnd = ref->GetLevel(0).GetNumEdges(); eid < eEnd; ++eid)
    {
        auto coarseVerts = ref->GetLevel(0).GetEdgeVertices(eid);
        if (coarseVerts.size() != 2)
            continue;

        const auto refinedEdges = expandEdgeToLevel(eid);
        for (int rid : refinedEdges)
        {
            auto ev = L.GetEdgeVertices(rid);
            if (ev.size() == 2)
                result.emplace_back((uint32_t)ev[0], (uint32_t)ev[1]);
        }
    }

    return result;
}
