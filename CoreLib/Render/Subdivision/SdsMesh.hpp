#pragma once

#include <cassert>
#include <glm/glm.hpp>
#include <memory>
#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <vector>

using IndexPair = std::pair<int32_t, int32_t>;

/**
 * @class SdsMesh
 * @brief Thin OpenSubdiv wrapper.
 *
 * Public API uses glm::vec2/vec3 vectors.
 * Internally it adapts glm values to OpenSubdiv Primvar requirements.
 */
class SdsMesh final
{
public:
    using Descriptor     = OpenSubdiv::Far::TopologyDescriptor;
    using IndexArray     = OpenSubdiv::Far::ConstIndexArray;
    using RefinerFactory = OpenSubdiv::Far::TopologyRefinerFactory<Descriptor>;

    SdsMesh()  = default;
    ~SdsMesh() = default;

    SdsMesh(const SdsMesh&)            = delete;
    SdsMesh& operator=(const SdsMesh&) = delete;

    SdsMesh(SdsMesh&&) noexcept            = default;
    SdsMesh& operator=(SdsMesh&&) noexcept = default;

    /** @brief Destroy refiner and reset to empty. */
    void clear() noexcept;

    /** @brief Create (or recreate) topology refiner from a descriptor. */
    void create(const Descriptor& desc);

    /** @brief Uniformly refine to max @p level (0..N). */
    void refine(int level);

    /** @brief Drop refined levels, keep base topology. */
    void unrefine() noexcept;

    /** @brief True if refiner exists. */
    bool valid() const noexcept;

    /** @brief Current max refinement level (0 if invalid). */
    int level() const noexcept;

    /** @brief Non-owning access to underlying refiner. */
    OpenSubdiv::Far::TopologyRefiner* refiner() const noexcept;

    // ---------------------------------------------------------------------
    // glm-facing interpolation helpers
    // ---------------------------------------------------------------------

    /**
     * @brief Interpolate vertex primvars across all built levels (glm::vec3).
     *
     * Input: `data` must contain at least level-0 vertex values.
     * Output: `data` becomes total-vertices-across-all-levels layout.
     */
    void interpolate(std::vector<glm::vec3>& data) const;

    /**
     * @brief Interpolate face-varying primvars across all built levels (glm::vec2).
     *
     * Input: `data` must contain at least level-0 fvar values for `channel`.
     * Output: `data` becomes total-fvars-across-all-levels layout.
     */
    void interpolate_face_varying(std::vector<glm::vec2>& data, int channel = 0) const;

    /**
     * @brief Interpolate face-uniform primvars across all built levels.
     *
     * Works for POD-like types (e.g. int/material IDs).
     * Input: level-0 values; Output: total-faces-across-all-levels layout.
     */
    template<typename T>
    void interpolate_face_uniform(std::vector<T>& data) const;

    // ---------------------------------------------------------------------
    // Topology queries at current max level
    // ---------------------------------------------------------------------

    int num_verts() const noexcept;
    int num_fvars(int channel = 0) const noexcept;
    int num_edges() const noexcept;
    int num_faces() const noexcept;
    int num_channels() const noexcept;

    IndexArray edge(int n) const noexcept;
    IndexArray face_verts(int n) const noexcept;
    IndexArray face_fvars(int n, int channel = 0) const noexcept;

    // ---------------------------------------------------------------------
    // Base->limit helpers (level-0 -> current max level)
    // ---------------------------------------------------------------------

    int              limit_vert(int vertIndex) const;
    std::vector<int> limit_edges(int edgeIndex) const;
    std::vector<int> limit_edges(IndexPair edge) const;
    std::vector<int> limit_polys(int polyIndex) const;
    std::vector<int> limit_poly_edges(int polyIndex) const;
    int              limit_poly_center(int polyIndex) const;

    /** @brief Find internal OSD level-0 edge index for (v0,v1), or -1. */
    int find_edge(int v0, int v1) const;

private:
    std::unique_ptr<OpenSubdiv::Far::TopologyRefiner> m_refiner = {};

    // Minimal OSD primvar adapter (hidden; keeps your public API glm-only).
    template<typename V>
    struct OsdPrimvar
    {
        V value{};

        void Clear(void* = nullptr) noexcept
        {
            value = V{0};
        }
        void AddWithWeight(const OsdPrimvar& src, float w) noexcept
        {
            value += src.value * w;
        }
        OsdPrimvar& operator=(const V& v) noexcept
        {
            value = v;
            return *this;
        }
        operator V() const noexcept
        {
            return value;
        }
    };
};

// ============================================================================
// Implementation
// ============================================================================

inline void SdsMesh::clear() noexcept
{
    m_refiner.reset();
}

inline bool SdsMesh::valid() const noexcept
{
    return m_refiner != nullptr;
}

inline int SdsMesh::level() const noexcept
{
    return m_refiner ? m_refiner->GetMaxLevel() : 0;
}

inline OpenSubdiv::Far::TopologyRefiner* SdsMesh::refiner() const noexcept
{
    return m_refiner.get();
}

inline void SdsMesh::create(const Descriptor& desc)
{
    using OpenSubdiv::Sdc::Options;

    Options s = {};

    s.SetVtxBoundaryInterpolation(Options::VTX_BOUNDARY_EDGE_ONLY);
    s.SetTriangleSubdivision(Options::TRI_SUB_SMOOTH);
    s.SetCreasingMethod(Options::CREASE_UNIFORM);

    if (desc.numFVarChannels > 0)
        s.SetFVarLinearInterpolation(Options::FVAR_LINEAR_CORNERS_PLUS2);
    else
        s.SetFVarLinearInterpolation(Options::FVAR_LINEAR_NONE);

    auto opts                 = RefinerFactory::Options(OpenSubdiv::Sdc::SCHEME_CATMARK, s);
    opts.validateFullTopology = false;

    m_refiner.reset(RefinerFactory::Create(desc, opts));
}

inline void SdsMesh::refine(int level)
{
    if (!m_refiner)
        return;

    if (level < 0)
        level = 0;

    if (level == m_refiner->GetMaxLevel())
        return;

    m_refiner->Unrefine();

    OpenSubdiv::Far::TopologyRefiner::UniformOptions options(level);
    options.fullTopologyInLastLevel = true;
    m_refiner->RefineUniform(options);
}

inline void SdsMesh::unrefine() noexcept
{
    if (m_refiner)
        m_refiner->Unrefine();
}

// ---------------------------------------------------------------------
// glm-facing interpolation
// ---------------------------------------------------------------------

inline void SdsMesh::interpolate(std::vector<glm::vec3>& data) const
{
    if (!m_refiner)
        return;

    const int l0 = m_refiner->GetLevel(0).GetNumVertices();
    if ((int)data.size() < l0)
        data.resize((size_t)l0, glm::vec3(0.0f));

    // Convert glm -> OsdPrimvar (L0)
    std::vector<OsdPrimvar<glm::vec3>> pv((size_t)l0);
    for (int i = 0; i < l0; ++i)
        pv[(size_t)i] = data[(size_t)i];

    // Resize to total and interpolate in-place
    const int total = m_refiner->GetNumVerticesTotal();
    pv.resize((size_t)total);

    auto* src = pv.data();
    auto* dst = src + l0;

    OpenSubdiv::Far::PrimvarRefiner prim(*m_refiner);

    for (int lvl = 1; lvl <= m_refiner->GetMaxLevel(); ++lvl)
    {
        prim.Interpolate(lvl, src, dst);
        src = dst;
        dst += m_refiner->GetLevel(lvl).GetNumVertices();
    }

    // Convert back OsdPrimvar -> glm
    data.resize((size_t)total);
    for (int i = 0; i < total; ++i)
        data[(size_t)i] = (glm::vec3)pv[(size_t)i];
}

inline void SdsMesh::interpolate_face_varying(std::vector<glm::vec2>& data, int channel) const
{
    if (!m_refiner)
        return;

    const int ch = num_channels();
    if (channel < 0 || channel >= ch)
        return;

    const int fvarL0 = m_refiner->GetLevel(0).GetNumFVarValues(channel);
    if ((int)data.size() < fvarL0)
        data.resize((size_t)fvarL0, glm::vec2(0.0f));

    // Convert glm -> OsdPrimvar (L0)
    std::vector<OsdPrimvar<glm::vec2>> pv((size_t)fvarL0);
    for (int i = 0; i < fvarL0; ++i)
        pv[(size_t)i] = data[(size_t)i];

    // Resize to total and interpolate
    const int total = m_refiner->GetNumFVarValuesTotal(channel);
    pv.resize((size_t)total);

    auto* src = pv.data();
    auto* dst = src + fvarL0;

    OpenSubdiv::Far::PrimvarRefiner prim(*m_refiner);

    for (int lvl = 1; lvl <= m_refiner->GetMaxLevel(); ++lvl)
    {
        prim.InterpolateFaceVarying(lvl, src, dst, channel);
        src = dst;
        dst += m_refiner->GetLevel(lvl).GetNumFVarValues(channel);
    }

    // Convert back
    data.resize((size_t)total);
    for (int i = 0; i < total; ++i)
        data[(size_t)i] = (glm::vec2)pv[(size_t)i];
}

template<typename T>
inline void SdsMesh::interpolate_face_uniform(std::vector<T>& data) const
{
    if (!m_refiner)
        return;

    const int l0 = m_refiner->GetLevel(0).GetNumFaces();
    if ((int)data.size() < l0)
        data.resize((size_t)l0);

    const int total = m_refiner->GetNumFacesTotal();
    data.resize((size_t)total);

    T* src = data.data();
    T* dst = src + l0;

    OpenSubdiv::Far::PrimvarRefiner prim(*m_refiner);

    for (int lvl = 1; lvl <= m_refiner->GetMaxLevel(); ++lvl)
    {
        prim.InterpolateFaceUniform(lvl, src, dst);
        src = dst;
        dst += m_refiner->GetLevel(lvl).GetNumFaces();
    }
}

// ---------------------------------------------------------------------
// Topology queries (max level)
// ---------------------------------------------------------------------

inline int SdsMesh::num_verts() const noexcept
{
    return m_refiner ? m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetNumVertices() : 0;
}

inline int SdsMesh::num_channels() const noexcept
{
    return m_refiner ? m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetNumFVarChannels() : 0;
}

inline int SdsMesh::num_fvars(int channel) const noexcept
{
    if (!m_refiner)
        return 0;

    if (channel < 0 || channel >= num_channels())
        return 0;

    return m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetNumFVarValues(channel);
}

inline int SdsMesh::num_edges() const noexcept
{
    return m_refiner ? m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetNumEdges() : 0;
}

inline int SdsMesh::num_faces() const noexcept
{
    return m_refiner ? m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetNumFaces() : 0;
}

inline SdsMesh::IndexArray SdsMesh::edge(int n) const noexcept
{
    return m_refiner ? m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetEdgeVertices(n) : IndexArray();
}

inline SdsMesh::IndexArray SdsMesh::face_verts(int n) const noexcept
{
    return m_refiner ? m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetFaceVertices(n) : IndexArray();
}

inline SdsMesh::IndexArray SdsMesh::face_fvars(int n, int channel) const noexcept
{
    if (!m_refiner)
        return IndexArray();

    if (channel < 0 || channel >= num_channels())
        return IndexArray();

    return m_refiner->GetLevel(m_refiner->GetMaxLevel()).GetFaceFVarValues(n, channel);
}

// ---------------------------------------------------------------------
// Base->limit helpers
// ---------------------------------------------------------------------

inline int SdsMesh::limit_vert(int vertIndex) const
{
    if (!m_refiner)
        return vertIndex;

    for (int l = 1; l <= level(); ++l)
        vertIndex = m_refiner->GetLevel(l - 1).GetVertexChildVertex(vertIndex);

    return vertIndex;
}

inline std::vector<int> SdsMesh::limit_edges(int edgeIndex) const
{
    std::vector<int> edges;
    if (!m_refiner)
        return edges;

    edges.push_back(edgeIndex);

    for (int l = 1; l <= level(); ++l)
    {
        std::vector<int> temp = edges;
        edges.clear();

        for (int e : temp)
        {
            auto children = m_refiner->GetLevel(l - 1).GetEdgeChildEdges(e);
            edges.insert(edges.end(), children.begin(), children.end());
        }
    }

    return edges;
}

inline std::vector<int> SdsMesh::limit_edges(IndexPair edge) const
{
    if (!m_refiner)
        return {};

    const int index = find_edge(edge.first, edge.second);
    assert(index != -1);
    if (index == -1)
        return {};

    return limit_edges(index);
}

inline std::vector<int> SdsMesh::limit_polys(int polyIndex) const
{
    std::vector<int> polys;
    if (!m_refiner)
        return polys;

    polys.push_back(polyIndex);

    for (int l = 1; l <= level(); ++l)
    {
        std::vector<int> temp = polys;
        polys.clear();

        for (int f : temp)
        {
            auto children = m_refiner->GetLevel(l - 1).GetFaceChildFaces(f);
            polys.insert(polys.end(), children.begin(), children.end());
        }
    }

    return polys;
}

inline std::vector<int> SdsMesh::limit_poly_edges(int polyIndex) const
{
    std::vector<int> edges;
    if (!m_refiner)
        return edges;

    auto face_edges = m_refiner->GetLevel(0).GetFaceEdges(polyIndex);

    for (int i = 0; i < face_edges.size(); ++i)
    {
        auto children = limit_edges(face_edges[i]);
        edges.insert(edges.end(), children.begin(), children.end());
    }

    return edges;
}

inline int SdsMesh::limit_poly_center(int polyIndex) const
{
    if (!m_refiner)
        return -1;

    auto faces = m_refiner->GetLevel(0).GetFaceChildFaces(polyIndex);
    if (faces.size() == 0)
        return -1;

    for (int vert : m_refiner->GetLevel(1).GetFaceVertices(faces[0]))
    {
        bool shared = true;

        for (int poly : m_refiner->GetLevel(1).GetVertexFaces(vert))
        {
            if (faces.FindIndex(poly) == -1)
            {
                shared = false;
                break;
            }
        }

        if (shared)
        {
            int res = vert;
            for (int l = 1; l < level(); ++l)
                res = m_refiner->GetLevel(l).GetVertexChildVertex(res);

            return res;
        }
    }

    return -1;
}

inline int SdsMesh::find_edge(int v0, int v1) const
{
    return m_refiner ? m_refiner->GetLevel(0).FindEdge(v0, v1) : -1;
}
