#include "HeMesh.hpp"

#include <cassert>
#include <unordered_map>
#include <unordered_set>

#include "HoleList.hpp"
#include "SmallList.hpp"
#include "SysMesh.hpp"

namespace
{
    struct EdgeKey
    {
        int32_t a{};
        int32_t b{};

        EdgeKey() = default;

        EdgeKey(int32_t v0, int32_t v1)
        {
            if (v0 <= v1)
            {
                a = v0;
                b = v1;
            }
            else
            {
                a = v1;
                b = v0;
            }
        }

        bool operator==(const EdgeKey& other) const noexcept
        {
            return a == other.a && b == other.b;
        }
    };

    struct EdgeKeyHash
    {
        std::size_t operator()(const EdgeKey& k) const noexcept
        {
            std::size_t h1 = std::hash<int32_t>{}(k.a);
            std::size_t h2 = std::hash<int32_t>{}(k.b);
            return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2));
        }
    };
} // namespace

// =============================================================
// Internal data (PIMPL)
// =============================================================

struct HeMesh::HeMeshData
{
    using VertId = HeMesh::VertId;
    using EdgeId = HeMesh::EdgeId;
    using PolyId = HeMesh::PolyId;
    using LoopId = HeMesh::LoopId;

    static constexpr VertId kInvalidVert = HeMesh::kInvalidVert;
    static constexpr EdgeId kInvalidEdge = HeMesh::kInvalidEdge;
    static constexpr PolyId kInvalidPoly = HeMesh::kInvalidPoly;
    static constexpr LoopId kInvalidLoop = HeMesh::kInvalidLoop;

    // ---------------------------------------------------------
    // Elements
    // ---------------------------------------------------------
    struct Vert
    {
        glm::vec3 position{0.0f};
        bool      removed{false};
        bool      selected{false};
        un::small_list<LoopId, 8> loops; // incident loops (vertex fan)
    };

    struct Edge
    {
        VertId v0{kInvalidVert};
        VertId v1{kInvalidVert};
        LoopId anyLoop{kInvalidLoop};

        bool removed{false};
        bool selected{false};
    };

    struct Poly
    {
        LoopId   firstLoop{kInvalidLoop};
        uint32_t materialId{0};

        bool removed{false};
        bool selected{false};
    };

    struct Loop
    {
        VertId vert{kInvalidVert};
        EdgeId edge{kInvalidEdge};
        PolyId poly{kInvalidPoly};

        LoopId next{kInvalidLoop}; // polygon ring
        LoopId prev{kInvalidLoop};

        LoopId radialNext{kInvalidLoop}; // edge fan
        LoopId radialPrev{kInvalidLoop};

        bool removed{false};

        // per-loop attributes
        glm::vec2 uv{0.0f, 0.0f};
        glm::vec3 normal{0.0f, 0.0f, 0.0f};
        bool      hasUV{false};
        bool      hasNormal{false};
    };

    // ---------------------------------------------------------
    // Storage
    // ---------------------------------------------------------
    HoleList<Vert> verts;
    HoleList<Edge> edges;
    HoleList<Poly> polys;
    HoleList<Loop> loops;

    std::unordered_map<EdgeKey, EdgeId, EdgeKeyHash> edgeMap; // undirected {v0,v1} -> EdgeId

    // ---------------------------------------------------------
    // Creation helpers
    // ---------------------------------------------------------
    VertId createVert(const glm::vec3& pos)
    {
        Vert v;
        v.position = pos;
        v.removed  = false;
        v.selected = false;

        const auto idx = verts.insert(v);
        return static_cast<VertId>(idx);
    }

    EdgeId createEdge(VertId v0, VertId v1)
    {
        Edge e;
        e.v0       = v0;
        e.v1       = v1;
        e.anyLoop  = kInvalidLoop;
        e.removed  = false;
        e.selected = false;

        const auto   idx = edges.insert(e);
        const EdgeId id  = static_cast<EdgeId>(idx);

        edgeMap.emplace(EdgeKey(v0, v1), id);
        return id;
    }

    PolyId createPoly()
    {
        Poly p;
        p.firstLoop  = kInvalidLoop;
        p.materialId = 0;
        p.removed    = false;
        p.selected   = false;

        const auto idx = polys.insert(p);
        return static_cast<PolyId>(idx);
    }

    LoopId createLoop(PolyId poly, VertId vert, EdgeId edge)
    {
        Loop l;
        l.vert       = vert;
        l.edge       = edge;
        l.poly       = poly;
        l.next       = kInvalidLoop;
        l.prev       = kInvalidLoop;
        l.radialNext = kInvalidLoop;
        l.radialPrev = kInvalidLoop;
        l.removed    = false;
        l.hasUV      = false;
        l.hasNormal  = false;

        const auto   idx = loops.insert(l);
        const LoopId id  = static_cast<LoopId>(idx);

        addLoopToVert(vert, id);
        return id;
    }

    // ---------------------------------------------------------
    // Loop / edge connectivity helpers
    // ---------------------------------------------------------

    LoopId polyFirstLoop(PolyId p) const noexcept
    {
        return polys[p].firstLoop;
    }

    void setPolyFirstLoop(PolyId p, LoopId l) noexcept
    {
        polys[p].firstLoop = l;
    }

    VertId loopVert(LoopId l) const noexcept
    {
        return loops[l].vert;
    }

    EdgeId loopEdge(LoopId l) const noexcept
    {
        return loops[l].edge;
    }

    PolyId loopPoly(LoopId l) const noexcept
    {
        return loops[l].poly;
    }

    LoopId loopNext(LoopId l) const noexcept
    {
        return loops[l].next;
    }

    LoopId loopPrev(LoopId l) const noexcept
    {
        return loops[l].prev;
    }

    LoopId loopRadialNext(LoopId l) const noexcept
    {
        return loops[l].radialNext;
    }

    LoopId loopRadialPrev(LoopId l) const noexcept
    {
        return loops[l].radialPrev;
    }

    void setLoopNextPrev(LoopId l, LoopId next, LoopId prev) noexcept
    {
        Loop& ref = loops[l];
        ref.next  = next;
        ref.prev  = prev;
    }

    void setLoopRadialNextPrev(LoopId l,
                               LoopId radialNext,
                               LoopId radialPrev) noexcept
    {
        Loop& ref      = loops[l];
        ref.radialNext = radialNext;
        ref.radialPrev = radialPrev;
    }

    LoopId edgeAnyLoop(EdgeId e) const noexcept
    {
        return edges[e].anyLoop;
    }

    void setEdgeAnyLoop(EdgeId e, LoopId l) noexcept
    {
        edges[e].anyLoop = l;
    }

    // Attach loop l into edge e's radial ring
    void attachLoopToEdge(EdgeId e, LoopId l) noexcept
    {
        Edge& ed = edges[e];
        if (ed.anyLoop == kInvalidLoop)
        {
            ed.anyLoop          = l;
            loops[l].radialNext = l;
            loops[l].radialPrev = l;
        }
        else
        {
            LoopId a = ed.anyLoop;
            LoopId b = loops[a].radialNext;

            loops[l].radialPrev = a;
            loops[l].radialNext = b;
            loops[a].radialNext = l;
            loops[b].radialPrev = l;
        }
        loops[l].edge = e;
    }

    // Detach loop l from edge e's radial ring (does not delete l)
    void detachLoopFromEdge(EdgeId e, LoopId l) noexcept
    {
        Edge&  ed = edges[e];
        Loop&  L  = loops[l];
        LoopId pr = L.radialPrev;
        LoopId nx = L.radialNext;

        if (pr == l && nx == l)
        {
            // single element
            ed.anyLoop = kInvalidLoop;
        }
        else
        {
            loops[pr].radialNext = nx;
            loops[nx].radialPrev = pr;
            if (ed.anyLoop == l)
                ed.anyLoop = nx;
        }

        L.radialNext = kInvalidLoop;
        L.radialPrev = kInvalidLoop;
    }

    // Find the "twin" of loop l on the same edge but different polygon.
    // Returns kInvalidLoop if no other polygon shares this edge (boundary)
    // or if radial is degenerate.
    LoopId twinLoop(LoopId l) const noexcept
    {
        const Loop& L = loops[l];
        EdgeId      e = L.edge;
        if (e == kInvalidEdge)
            return kInvalidLoop;

        // Single-polys or unlinked radial: no twin
        LoopId start = loops[l].radialNext;
        if (start == kInvalidLoop || start == l)
            return kInvalidLoop;

        LoopId cur = start;
        do
        {
            if (loops[cur].poly != L.poly)
                return cur; // polygon differs → twin
            cur = loops[cur].radialNext;
        }
        while (cur != l && cur != kInvalidLoop);

        return kInvalidLoop;
    }

    void addLoopToVert(VertId v, LoopId l) noexcept
    {
        if (v == kInvalidVert)
            return;

        verts[v].loops.push_back(l);
    }

    void removeLoopFromVert(VertId v, LoopId l) noexcept
    {
        if (v == kInvalidVert)
            return;

        verts[v].loops.erase_element(l);
    }
};

// =============================================================
// HeMesh basic API
// =============================================================

HeMesh::HeMesh() : m_data(std::make_unique<HeMeshData>())
{
}

HeMesh::~HeMesh() = default;
HeMesh::HeMesh(HeMesh&& other) noexcept            = default;
HeMesh& HeMesh::operator=(HeMesh&& other) noexcept = default;

void HeMesh::clear() noexcept
{
    if (!m_data)
        return;

    m_data->verts.clear();
    m_data->edges.clear();
    m_data->polys.clear();
    m_data->loops.clear();
    m_data->edgeMap.clear();
}

void HeMesh::reserve(int32_t vertCount,
                     int32_t edgeCount,
                     int32_t polyCount,
                     int32_t loopCount)
{
    if (!m_data)
        return;

    if (vertCount > 0)
        m_data->verts.reserve(vertCount);
    if (edgeCount > 0)
        m_data->edges.reserve(edgeCount);
    if (polyCount > 0)
        m_data->polys.reserve(polyCount);
    if (loopCount > 0)
        m_data->loops.reserve(loopCount);
}

int32_t HeMesh::vertCount() const noexcept
{
    if (!m_data)
        return 0;
    return static_cast<int32_t>(m_data->verts.valid_indices().size());
}

int32_t HeMesh::edgeCount() const noexcept
{
    if (!m_data)
        return 0;
    return static_cast<int32_t>(m_data->edges.valid_indices().size());
}

int32_t HeMesh::polyCount() const noexcept
{
    if (!m_data)
        return 0;
    return static_cast<int32_t>(m_data->polys.valid_indices().size());
}

int32_t HeMesh::loopCount() const noexcept
{
    if (!m_data)
        return 0;
    return static_cast<int32_t>(m_data->loops.valid_indices().size());
}

std::span<const HeMesh::VertId> HeMesh::allVerts() const noexcept
{
    if (!m_data)
        return {};

    const auto& ids = m_data->verts.valid_indices();
    return std::span<const VertId>(ids.data(), ids.size());
}

std::span<const HeMesh::EdgeId> HeMesh::allEdges() const noexcept
{
    if (!m_data)
        return {};

    const auto& ids = m_data->edges.valid_indices();
    return std::span<const EdgeId>(ids.data(), ids.size());
}

std::span<const HeMesh::PolyId> HeMesh::allPolys() const noexcept
{
    if (!m_data)
        return {};

    const auto& ids = m_data->polys.valid_indices();
    return std::span<const PolyId>(ids.data(), ids.size());
}

// =============================================================
// Low-level loop access (public wrappers)
// =============================================================

HeMesh::LoopId HeMesh::polyFirstLoopId(PolyId p) const noexcept
{
    if (!m_data || !polyValid(p))
        return kInvalidLoop;

    return m_data->polyFirstLoop(p);
}

HeMesh::LoopId HeMesh::edgeAnyLoopId(EdgeId e) const noexcept
{
    if (!m_data || !edgeValid(e))
        return kInvalidLoop;

    return m_data->edgeAnyLoop(e);
}

HeMesh::VertId HeMesh::loopVertId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidVert;

    return m_data->loopVert(l);
}

HeMesh::EdgeId HeMesh::loopEdgeId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidEdge;

    return m_data->loopEdge(l);
}

HeMesh::PolyId HeMesh::loopPolyId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidPoly;

    return m_data->loopPoly(l);
}

HeMesh::LoopId HeMesh::loopNextId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidLoop;

    return m_data->loopNext(l);
}

HeMesh::LoopId HeMesh::loopPrevId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidLoop;

    return m_data->loopPrev(l);
}

HeMesh::LoopId HeMesh::loopRadialNextId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidLoop;

    return m_data->loopRadialNext(l);
}

HeMesh::LoopId HeMesh::loopRadialPrevId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidLoop;

    return m_data->loopRadialPrev(l);
}

HeMesh::LoopId HeMesh::loopTwinId(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return kInvalidLoop;

    return m_data->twinLoop(l);
}

// =============================================================
// Directed loop find + insertVertOnPolyEdge
// =============================================================

HeMesh::LoopId HeMesh::findLoop(PolyId p, VertId a, VertId b) const noexcept
{
    if (!m_data || !polyValid(p) || !vertValid(a) || !vertValid(b) || a == b)
        return kInvalidLoop;

    LoopId start = m_data->polyFirstLoop(p);
    if (start == kInvalidLoop || !loopValid(start))
        return kInvalidLoop;

    LoopId l = start;
    do
    {
        if (!loopValid(l))
            break;

        const VertId v0 = m_data->loopVert(l);
        if (v0 == a)
        {
            const LoopId ln = m_data->loopNext(l);
            if (loopValid(ln))
            {
                const VertId v1 = m_data->loopVert(ln);
                if (v1 == b)
                    return l;
            }
        }

        l = m_data->loopNext(l);
    }
    while (l != start && l != kInvalidLoop);

    return kInvalidLoop;
}

bool HeMesh::insertVertOnPolyEdge(PolyId p, VertId a, VertId b, VertId vNew)
{
    if (!m_data || !polyValid(p))
        return false;

    if (!vertValid(a) || !vertValid(b) || !vertValid(vNew))
        return false;

    if (a == b || a == vNew || b == vNew)
        return false;

    // Find directed (a -> b) or (b -> a) in this polygon.
    LoopId l       = findLoop(p, a, b);
    bool   flipped = false;

    if (l == kInvalidLoop)
    {
        l       = findLoop(p, b, a);
        flipped = true;
    }

    if (l == kInvalidLoop)
        return false;

    // After this, l is the loop whose directed edge is (s -> t) in polygon p,
    // where s = loopVert(l), t = loopVert(next(l)).
    const VertId s  = m_data->loopVert(l);
    const LoopId ln = m_data->loopNext(l);
    if (!loopValid(ln))
        return false;

    const VertId t = m_data->loopVert(ln);
    if (!vertValid(s) || !vertValid(t) || s == t)
        return false;

    // We are inserting vNew between (s,t), so edges become (s,vNew) and (vNew,t).
    const EdgeId oldEdge = m_data->loopEdge(l);
    if (!edgeValid(oldEdge))
        return false;

    const EdgeId e0 = ensureEdge(s, vNew);
    const EdgeId e1 = ensureEdge(vNew, t);

    if (!edgeValid(e0) || !edgeValid(e1))
        return false;

    // Preserve/derive per-loop attributes for the new corner.
    // Corner attributes live on loops: loop l corresponds to corner at vertex 's',
    // and loop ln corresponds to corner at vertex 't'. We'll create a new loop m at vertex vNew.
    const bool hasUV0 = loopHasUV(l);
    const bool hasUV1 = loopHasUV(ln);
    const bool hasN0  = loopHasNormal(l);
    const bool hasN1  = loopHasNormal(ln);

    glm::vec2 uvMid(0.0f);
    glm::vec3 nMid(0.0f);

    if (hasUV0 && hasUV1)
        uvMid = 0.5f * (loopUV(l) + loopUV(ln));
    if (hasN0 && hasN1)
        nMid = glm::normalize(loopNormal(l) + loopNormal(ln));

    // 1) Detach loop l from old edge radial fan and attach to new edge e0.
    m_data->detachLoopFromEdge(oldEdge, l);
    m_data->attachLoopToEdge(e0, l);

    // Make sure loop l references its new edge.
    m_data->loops[l].edge = e0;

    // 2) Create new loop m for (vNew -> t) and insert it between l and ln in the polygon ring.
    //    Polygon ring: l -> ln becomes l -> m -> ln.
    LoopId m = m_data->createLoop(p, vNew, e1);
    if (!loopValid(m))
        return false;

    // Set ring links
    m_data->setLoopNextPrev(m, ln, l);
    m_data->loops[l].next  = m;
    m_data->loops[ln].prev = m;

    // Attach m to edge e1 radial fan
    // createLoop() already added it to vNew fan; radial fan attach is done via attachLoopToEdge().
    // But our createLoop does NOT radial-attach; it only sets loop.edge to e1.
    // So we must attach it now:
    m_data->attachLoopToEdge(e1, m);

    // Set face-varying attributes on the new loop if we could derive them.
    if (hasUV0 && hasUV1)
        setLoopUV(m, uvMid);
    if (hasN0 && hasN1)
        setLoopNormal(m, nMid);

    // If the old edge is now unused, it will be cleaned by removeUnusedEdges().
    // We do NOT auto-remove here because other polygons may still use it.

    (void)flipped; // kept for debugging if you want, not required logically
    return true;
}

// =============================================================
// Validity
// =============================================================

bool HeMesh::vertValid(VertId v) const noexcept
{
    return m_data &&
           v >= 0 &&
           v < static_cast<VertId>(m_data->verts.capacity()) &&
           !m_data->verts[v].removed;
}

bool HeMesh::edgeValid(EdgeId e) const noexcept
{
    return m_data &&
           e >= 0 &&
           e < static_cast<EdgeId>(m_data->edges.capacity()) &&
           !m_data->edges[e].removed;
}

bool HeMesh::polyValid(PolyId p) const noexcept
{
    return m_data &&
           p >= 0 &&
           p < static_cast<PolyId>(m_data->polys.capacity()) &&
           !m_data->polys[p].removed;
}

bool HeMesh::loopValid(LoopId l) const noexcept
{
    return m_data &&
           l >= 0 &&
           l < static_cast<LoopId>(m_data->loops.capacity()) &&
           !m_data->loops[l].removed;
}

// =============================================================
// Geometry
// =============================================================

glm::vec3 HeMesh::position(VertId v) const noexcept
{
    return m_data->verts[v].position;
}

void HeMesh::setPosition(VertId v, const glm::vec3& pos) noexcept
{
    m_data->verts[v].position = pos;
}

uint32_t HeMesh::polyMaterial(PolyId p) const noexcept
{
    return m_data->polys[p].materialId;
}

void HeMesh::setPolyMaterial(PolyId p, uint32_t materialId) noexcept
{
    m_data->polys[p].materialId = materialId;
}

glm::vec3 HeMesh::polyNormal(PolyId pid) const noexcept
{
    auto v = polyVerts(pid);
    if (v.size() < 3)
        return glm::vec3(0, 0, 1);

    glm::vec3 p0 = position(v[0]);
    glm::vec3 N(0.0f);

    for (int32_t i = 1; i + 1 < v.size(); ++i)
    {
        glm::vec3 a = position(v[i]) - p0;
        glm::vec3 b = position(v[i + 1]) - p0;
        N += glm::cross(a, b);
    }

    float len2 = glm::dot(N, N);
    if (len2 < 1e-12f)
        return glm::vec3(0, 0, 1);

    return N / std::sqrt(len2);
}

// =============================================================
// Per-loop (face-varying) attributes
// =============================================================

bool HeMesh::loopHasUV(LoopId l) const noexcept
{
    return m_data && loopValid(l) && m_data->loops[l].hasUV;
}

glm::vec2 HeMesh::loopUV(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return glm::vec2(0.0f, 0.0f);

    return m_data->loops[l].uv;
}

void HeMesh::setLoopUV(LoopId l, const glm::vec2& uv) noexcept
{
    if (!m_data || !loopValid(l))
        return;

    auto& L = m_data->loops[l];
    L.uv    = uv;
    L.hasUV = true;
}

void HeMesh::clearLoopUV(LoopId l) noexcept
{
    if (!m_data || !loopValid(l))
        return;

    auto& L = m_data->loops[l];
    L.uv    = glm::vec2(0.0f, 0.0f);
    L.hasUV = false;
}

bool HeMesh::loopHasNormal(LoopId l) const noexcept
{
    return m_data && loopValid(l) && m_data->loops[l].hasNormal;
}

glm::vec3 HeMesh::loopNormal(LoopId l) const noexcept
{
    if (!m_data || !loopValid(l))
        return glm::vec3(0.0f, 0.0f, 0.0f);

    return m_data->loops[l].normal;
}

void HeMesh::setLoopNormal(LoopId l, const glm::vec3& n) noexcept
{
    if (!m_data || !loopValid(l))
        return;

    auto& L     = m_data->loops[l];
    L.normal    = n;
    L.hasNormal = true;
}

void HeMesh::clearLoopNormal(LoopId l) noexcept
{
    if (!m_data || !loopValid(l))
        return;

    auto& L     = m_data->loops[l];
    L.normal    = glm::vec3(0.0f, 0.0f, 0.0f);
    L.hasNormal = false;
}

// =============================================================
// Creation / removal
// =============================================================

HeMesh::VertId HeMesh::createVert(const glm::vec3& pos)
{
    return m_data->createVert(pos);
}

HeMesh::EdgeId HeMesh::ensureEdge(VertId v0, VertId v1)
{
    if (!vertValid(v0) || !vertValid(v1) || v0 == v1)
        return kInvalidEdge;

    EdgeKey key(v0, v1);

    auto it = m_data->edgeMap.find(key);
    if (it != m_data->edgeMap.end())
        return it->second;

    return m_data->createEdge(v0, v1);
}

HeMesh::PolyId HeMesh::createPoly(const std::vector<VertId>& verts,
                                  uint32_t                   materialId)
{
    const int n = static_cast<int>(verts.size());
    if (n < 3)
        return kInvalidPoly;

    PolyId pid                    = m_data->createPoly();
    m_data->polys[pid].materialId = materialId;

    std::vector<LoopId> loops;
    loops.reserve(static_cast<std::size_t>(n));

    // First pass: create loops and attach to edges
    for (int i = 0; i < n; ++i)
    {
        VertId v0 = verts[i];
        VertId v1 = verts[(i + 1) % n];

        if (!vertValid(v0) || !vertValid(v1))
        {
            m_data->polys[pid].removed = true;
            return kInvalidPoly;
        }

        EdgeId e = ensureEdge(v0, v1);
        if (!edgeValid(e))
        {
            m_data->polys[pid].removed = true;
            return kInvalidPoly;
        }

        LoopId l = m_data->createLoop(pid, v0, e);
        loops.push_back(l);

        // radial link
        LoopId any = m_data->edgeAnyLoop(e);
        if (any == kInvalidLoop)
        {
            m_data->setEdgeAnyLoop(e, l);
            m_data->loops[l].radialNext = l;
            m_data->loops[l].radialPrev = l;
        }
        else
        {
            LoopId a = any;
            LoopId b = m_data->loopRadialNext(a);

            m_data->loops[l].radialPrev = a;
            m_data->loops[l].radialNext = b;
            m_data->loops[a].radialNext = l;
            m_data->loops[b].radialPrev = l;
        }
    }

    // Polygon ring links
    for (int i = 0; i < n; ++i)
    {
        LoopId cur  = loops[i];
        LoopId next = loops[(i + 1) % n];
        LoopId prev = loops[(i + n - 1) % n];

        m_data->setLoopNextPrev(cur, next, prev);
    }

    m_data->setPolyFirstLoop(pid, loops.front());
    return pid;
}

void HeMesh::removeVert(VertId v)
{
    if (!vertValid(v))
        return;

    assert(m_data->verts[v].loops.empty() && "removeVert() only valid for isolated vertices. Delete faces/edges then cleanup.");
    if (!m_data->verts[v].loops.empty())
        return;

    m_data->verts[v].removed = true;
    m_data->verts.remove(v);
}

void HeMesh::removeEdge(EdgeId e)
{
    if (!edgeValid(e))
        return;

    auto&   E = m_data->edges[e];
    EdgeKey key(E.v0, E.v1);

    // Erase from map first
    m_data->edgeMap.erase(key);

    E.removed = true;
    E.anyLoop = HeMeshData::kInvalidLoop;
    E.v0      = HeMeshData::kInvalidVert;
    E.v1      = HeMeshData::kInvalidVert;

    m_data->edges.remove(e);
}

void HeMesh::removeUnusedEdges()
{
    if (!m_data)
        return;

    const int32_t cap = static_cast<int32_t>(m_data->edges.capacity());

    for (EdgeId e = 0; e < cap; ++e)
    {
        if (!edgeValid(e))
            continue;

        // If an edge has no loops in its radial fan, it is unused.
        if (m_data->edges[e].anyLoop == kInvalidLoop)
        {
            removeEdge(e);
        }
        else
        {
            // Optional: self-heal in case anyLoop points to an invalid loop.
            // This can happen if something removed loops without updating edges.
            LoopId l = m_data->edges[e].anyLoop;
            if (!loopValid(l) || m_data->loops[l].edge != e)
            {
                // Try to find any surviving loop that still references this edge.
                LoopId found = kInvalidLoop;

                // Walk the loop storage (capacity scan); cheap enough for tools.
                const int32_t lcap = static_cast<int32_t>(m_data->loops.capacity());
                for (LoopId li = 0; li < lcap; ++li)
                {
                    if (!loopValid(li))
                        continue;

                    if (m_data->loops[li].edge == e)
                    {
                        found = li;
                        break;
                    }
                }

                if (found == kInvalidLoop)
                {
                    // Truly unused
                    removeEdge(e);
                }
                else
                {
                    // Restore edgeAnyLoop; radial ring consistency is still expected
                    // to be correct if loops were removed via proper APIs.
                    m_data->edges[e].anyLoop = found;
                }
            }
        }
    }
}

void HeMesh::removeIsolatedVerts()
{
    if (!m_data)
        return;

    const int32_t cap = static_cast<int32_t>(m_data->verts.capacity());

    for (VertId v = 0; v < cap; ++v)
    {
        if (!vertValid(v))
            continue;

        auto& vert  = m_data->verts[v];
        auto& loops = vert.loops;

        // Clean invalid or mismatched loop references
        for (;;)
        {
            bool removedAny = false;

            for (auto it = loops.begin(); it != loops.end(); ++it)
            {
                LoopId l = *it;
                if (!loopValid(l) || m_data->loops[l].vert != v)
                {
                    loops.erase(it);
                    removedAny = true;
                    break;
                }
            }

            if (!removedAny)
                break;
        }

        // If no loops remain, the vertex is isolated
        if (loops.empty())
        {
            vert.removed = true;
            m_data->verts.remove(v);
        }
    }
}

HeMesh::EdgeId HeMesh::findEdge(VertId v0, VertId v1) const noexcept
{
    if (!m_data || !vertValid(v0) || !vertValid(v1) || v0 == v1)
        return kInvalidEdge;

    auto it = m_data->edgeMap.find(EdgeKey(v0, v1));
    if (it == m_data->edgeMap.end())
        return kInvalidEdge;

    return it->second;
}

// Polygon removal with full loop/radial cleanup
void HeMesh::removePoly(PolyId p)
{
    if (!polyValid(p))
        return;

    auto loops = polyLoops(p);
    for (LoopId l : loops)
    {
        // detach from edge radial fan
        EdgeId e = m_data->loopEdge(l);
        if (edgeValid(e))
            m_data->detachLoopFromEdge(e, l);

        // detach from vertex fan
        HeMeshData::VertId v = m_data->loopVert(l);
        m_data->removeLoopFromVert(v, l);

        m_data->loops[l].removed = true;
        m_data->loops.remove(l);
    }

    m_data->polys[p].removed = true;
    m_data->polys.remove(p);
}

// =============================================================
// Simple modeling ops
// =============================================================

// splitEdge: works for manifold edges, any valence
HeMesh::VertId HeMesh::splitEdge(EdgeId e, float t)
{
    if (!edgeValid(e))
        return kInvalidVert;

    // Gather radial loops
    std::vector<LoopId> radial;
    {
        HeMeshData::LoopId start = m_data->edgeAnyLoop(e);
        if (start == kInvalidLoop)
            return kInvalidVert;

        HeMeshData::LoopId l = start;
        do
        {
            radial.push_back(l);
            l = m_data->loopRadialNext(l);
        }
        while (l != start && l != kInvalidLoop);
    }

    if (radial.empty())
        return kInvalidVert;

    // Use first loop to compute interpolated position
    LoopId l0 = radial.front();
    LoopId n0 = m_data->loopNext(l0);

    VertId va = m_data->loopVert(l0);
    VertId vb = m_data->loopVert(n0);

    glm::vec3 pa = position(va);
    glm::vec3 pb = position(vb);
    glm::vec3 pn = (1.0f - t) * pa + t * pb;

    VertId vNew = createVert(pn);

    // For each loop on this edge, we:
    // - detach it from old edge
    // - reattach it to new edge (va -> vNew)
    // - insert a new loop with (vNew -> vb)
    // - rewire polygon ring
    for (LoopId l : radial)
    {
        // compute polygon-local orientation
        LoopId next = m_data->loopNext(l);
        VertId v0   = m_data->loopVert(l);
        VertId v1   = m_data->loopVert(next);

        // new edges
        EdgeId e0 = ensureEdge(v0, vNew);
        EdgeId e1 = ensureEdge(vNew, v1);

        // detach l from old edge e
        m_data->detachLoopFromEdge(e, l);

        // attach l to e0
        m_data->attachLoopToEdge(e0, l);

        // update l to use v0->vNew
        m_data->loops[l].vert = v0;

        // create new loop for vNew->v1
        PolyId poly = m_data->loopPoly(l);
        LoopId m    = m_data->createLoop(poly, vNew, e1);

        // insert m in polygon ring between l and 'next'
        HeMeshData::LoopId oldNext = next;
        m_data->setLoopNextPrev(m, oldNext, l);
        m_data->loops[l].next       = m;
        m_data->loops[oldNext].prev = m;

        // radial for e1
        LoopId any1 = m_data->edgeAnyLoop(e1);
        if (any1 == kInvalidLoop)
        {
            m_data->setEdgeAnyLoop(e1, m);
            m_data->loops[m].radialNext = m;
            m_data->loops[m].radialPrev = m;
        }
        else
        {
            LoopId a = any1;
            LoopId b = m_data->loopRadialNext(a);

            m_data->loops[m].radialPrev = a;
            m_data->loops[m].radialNext = b;
            m_data->loops[a].radialNext = m;
            m_data->loops[b].radialPrev = m;
        }
    }

    // Old edge e now has no loops; mark removed
    removeEdge(e);
    return vNew;
}

HeMesh::VertId HeMesh::collapseEdge(EdgeId e)
{
    if (!edgeValid(e))
        return kInvalidVert;

    auto [v0, v1] = edgeVerts(e);
    if (!vertValid(v0) || !vertValid(v1))
        return kInvalidVert;

    glm::vec3 mid = 0.5f * (position(v0) + position(v1));
    setPosition(v0, mid);

    // Weld v1 into v0 (we'll fix vert adjacency later in weldVerts)
    weldVerts(v0, v1);

    // Remove edge fully (map + storage)
    removeEdge(e);
    return v0;
}

void HeMesh::dissolveEdge(EdgeId e)
{
    if (!edgeValid(e))
        return;

    auto polys = edgePolys(e);
    if (polys.size() != 2)
        return;

    const PolyId pA = polys[0];
    const PolyId pB = polys[1];
    if (!polyValid(pA) || !polyValid(pB))
        return;

    const uint32_t matA = polyMaterial(pA);

    // Find a loop in poly p whose loopEdge == e
    auto findAnyLoopOnEdgeInPoly = [&](PolyId p) -> LoopId {
        auto ls = polyLoops(p);
        for (LoopId l : ls)
        {
            if (!loopValid(l))
                continue;
            if (m_data->loopPoly(l) != p)
                continue;
            if (m_data->loopEdge(l) == e)
                return l;
        }
        return kInvalidLoop;
    };

    LoopId la = findAnyLoopOnEdgeInPoly(pA);
    LoopId lb = findAnyLoopOnEdgeInPoly(pB);
    if (la == kInvalidLoop || lb == kInvalidLoop)
        return;

    // For a directed loop l, the directed edge on that face is:
    //   start = vert(l)
    //   end   = vert(next(l))
    auto edge_end_vert = [&](LoopId l) -> VertId {
        const LoopId n = m_data->loopNext(l);
        if (!loopValid(n))
            return kInvalidVert;
        return m_data->loopVert(n);
    };

    const VertId a = m_data->loopVert(la);
    const VertId b = edge_end_vert(la);
    if (!vertValid(a) || !vertValid(b) || a == b)
        return;

    // We need poly B's loop on the SAME undirected edge but OPPOSITE direction (b -> a).
    // lb might already be (b->a). If not, its prev might be.
    auto is_dir = [&](LoopId l, VertId s, VertId t) -> bool {
        if (!loopValid(l))
            return false;
        if (m_data->loopVert(l) != s)
            return false;
        return edge_end_vert(l) == t;
    };

    LoopId lbOpp = kInvalidLoop;
    if (is_dir(lb, b, a))
        lbOpp = lb;
    else
    {
        // Try the loop immediately before lb in polygon order; that corresponds to the other directed edge instance.
        const LoopId lbPrev = m_data->loopPrev(lb);
        if (is_dir(lbPrev, b, a))
            lbOpp = lbPrev;
        else
        {
            // As a last resort, scan all loops of pB to find (b->a)
            auto ls = polyLoops(pB);
            for (LoopId l : ls)
            {
                if (is_dir(l, b, a))
                {
                    lbOpp = l;
                    break;
                }
            }
        }
    }

    if (lbOpp == kInvalidLoop)
        return; // cannot establish consistent direction => unsafe to dissolve

    // Walk boundary of polygon p excluding the shared edge (s->t),
    // producing vertices in order from t ... to s (inclusive).
    // Example: if edge is s->t, we start at loopNext(edgeLoop) which has vertex t,
    // then walk next until we reach the edgeLoop again, finally append s.
    auto boundary_excluding_directed_edge = [&](PolyId p, VertId s, VertId t) -> std::vector<VertId> {
        std::vector<VertId> out;

        // Find the directed edge loop (s->t) in this poly
        LoopId lEdge = kInvalidLoop;
        auto   ls    = polyLoops(p);
        for (LoopId l : ls)
        {
            if (!loopValid(l))
                continue;
            if (m_data->loopVert(l) != s)
                continue;
            if (edge_end_vert(l) == t)
            {
                lEdge = l;
                break;
            }
        }
        if (lEdge == kInvalidLoop)
            return out;

        // Start at the vertex after the edge: that's t.
        LoopId start = m_data->loopNext(lEdge);
        if (!loopValid(start))
            return out;

        LoopId cur = start;
        do
        {
            out.push_back(m_data->loopVert(cur));
            cur = m_data->loopNext(cur);
        }
        while (cur != lEdge && cur != kInvalidLoop);

        // Append s at the end (so sequence is t ... s)
        out.push_back(s);
        return out;
    };

    // A contributes boundary from b ... a (excluding a->b edge)
    const std::vector<VertId> chainA = boundary_excluding_directed_edge(pA, a, b);
    // B contributes boundary from a ... b (excluding b->a edge), so use directed (b->a) then we get a..b
    const std::vector<VertId> chainB = boundary_excluding_directed_edge(pB, b, a);

    if (chainA.size() < 3 || chainB.size() < 3)
        return;

    // Merge: chainA is [b ... a], chainB is [a ... b]
    // Concatenate skipping duplicate 'a', then drop closing duplicate 'b' if present.
    std::vector<VertId> merged;
    merged.reserve(chainA.size() + chainB.size());

    merged.insert(merged.end(), chainA.begin(), chainA.end());
    merged.insert(merged.end(), chainB.begin() + 1, chainB.end()); // skip duplicate 'a'

    // Remove consecutive duplicates (safety)
    {
        std::vector<VertId> cleaned;
        cleaned.reserve(merged.size());
        for (VertId v : merged)
        {
            if (cleaned.empty() || cleaned.back() != v)
                cleaned.push_back(v);
        }
        merged.swap(cleaned);
    }

    // If we closed explicitly (first == last), remove last
    if (merged.size() > 2 && merged.front() == merged.back())
        merged.pop_back();

    if (merged.size() < 3)
        return;

    // Remove original polys (properly detaches loops from radial + vert fans)
    removePoly(pA);
    removePoly(pB);

    // Create merged poly
    createPoly(merged, matA);

    // Cleanup
    removeUnusedEdges();
    removeIsolatedVerts();
}

HeMesh::VertId HeMesh::weldVerts(VertId keep, VertId kill)
{
    if (!vertValid(keep) || !vertValid(kill) || keep == kill)
        return kInvalidVert;

    auto& d = *m_data;

    // ---------------------------------------------------------
    // 1) Collect loops incident to 'kill' first
    // ---------------------------------------------------------
    std::vector<LoopId> killLoops;
    killLoops.reserve(16);

    for (LoopId l : d.verts[kill].loops)
        killLoops.push_back(l);

    // ---------------------------------------------------------
    // 2) Move loops: kill -> keep
    // ---------------------------------------------------------
    for (LoopId l : killLoops)
    {
        if (!loopValid(l))
            continue;

        d.loops[l].vert = keep;

        d.removeLoopFromVert(kill, l);
        d.addLoopToVert(keep, l);
    }

    // ---------------------------------------------------------
    // 3) Fix edges: replace kill with keep in Edge endpoints,
    //    and update edgeMap keys. Merge duplicates if necessary.
    // ---------------------------------------------------------
    // Collect all edges touching keep (after loop move) — cheap & safe.
    // We scan keep's incident loops and gather unique edges.
    std::unordered_set<EdgeId> incidentEdges;
    incidentEdges.reserve(64);

    for (LoopId l : d.verts[keep].loops)
    {
        if (!loopValid(l))
            continue;

        EdgeId e = d.loops[l].edge;
        if (edgeValid(e))
            incidentEdges.insert(e);
    }

    auto move_radial_loops = [&](EdgeId fromE, EdgeId toE) {
        // Move all radial loops from fromE onto toE.
        // Assumes both edges are valid, and fromE != toE.
        LoopId start = d.edges[fromE].anyLoop;
        if (start == kInvalidLoop)
            return;

        // Collect first (since we mutate radial pointers)
        std::vector<LoopId> rad;
        {
            LoopId l = start;
            do
            {
                if (loopValid(l))
                    rad.push_back(l);
                l = d.loops[l].radialNext;
            }
            while (l != start && l != kInvalidLoop);
        }

        for (LoopId l : rad)
        {
            if (!loopValid(l))
                continue;

            // Detach from old edge fan
            d.detachLoopFromEdge(fromE, l);

            // Attach to new edge fan (this also sets loops[l].edge = toE)
            d.attachLoopToEdge(toE, l);
        }

        // fromE should now be fan-empty
        d.edges[fromE].anyLoop = kInvalidLoop;
    };

    std::vector<EdgeId> edgesToRemove;
    edgesToRemove.reserve(16);

    for (EdgeId e : incidentEdges)
    {
        if (!edgeValid(e))
            continue;

        auto& E = d.edges[e];

        // If this edge still references kill, rewrite it to keep
        bool touched = false;

        if (E.v0 == kill)
        {
            E.v0    = keep;
            touched = true;
        }
        if (E.v1 == kill)
        {
            E.v1    = keep;
            touched = true;
        }

        if (!touched)
            continue;

        // Degenerate edge (keep-keep): this can happen in rare cases
        if (E.v0 == E.v1)
        {
            // Detach any loops (should not happen in sane manifold weld),
            // then mark unused; it will be removed by removeUnusedEdges().
            LoopId any = E.anyLoop;
            if (any != kInvalidLoop)
            {
                // best-effort: detach entire fan from this edge
                std::vector<LoopId> rad;
                {
                    LoopId l = any;
                    do
                    {
                        if (loopValid(l))
                            rad.push_back(l);
                        l = d.loops[l].radialNext;
                    }
                    while (l != any && l != kInvalidLoop);
                }

                for (LoopId l : rad)
                {
                    if (!loopValid(l))
                        continue;

                    d.detachLoopFromEdge(e, l);
                    d.loops[l].edge = kInvalidEdge;
                }
            }

            d.edges[e].anyLoop = kInvalidLoop;
            continue;
        }

        // Update edgeMap:
        // Remove old key(s) are tricky because we don't know what key was inserted.
        // So: erase by scanning the key that would correspond to current stored endpoints,
        // but also erase the "old" kill-key explicitly.
        d.edgeMap.erase(EdgeKey(kill, (E.v0 == keep) ? E.v1 : E.v0));
        d.edgeMap.erase(EdgeKey(E.v0, E.v1)); // in case it existed already

        EdgeKey newKey(E.v0, E.v1);

        auto it = d.edgeMap.find(newKey);
        if (it == d.edgeMap.end())
        {
            // No conflict, register this edge under new key
            d.edgeMap.emplace(newKey, e);
        }
        else
        {
            // Conflict: an edge (keep, other) already exists. Merge.
            EdgeId survivor = it->second;
            if (survivor != e && edgeValid(survivor))
            {
                move_radial_loops(e, survivor);
                edgesToRemove.push_back(e);
            }
            else
            {
                // If map points to us, ensure it's correct
                d.edgeMap[newKey] = e;
            }
        }
    }

    // Remove merged edges
    for (EdgeId e : edgesToRemove)
    {
        if (!edgeValid(e))
            continue;

        // At this point it should have no loops
        d.edges[e].anyLoop = kInvalidLoop;
        removeEdge(e);
    }

    // ---------------------------------------------------------
    // 4) Remove kill vert (should be isolated now)
    // ---------------------------------------------------------
    d.verts[kill].loops.clear(); // should already be empty, but ensure
    d.verts[kill].removed = true;
    d.verts.remove(kill);

    return keep;
}

// =============================================================
// High-level adjacency helpers
// =============================================================

std::pair<HeMesh::VertId, HeMesh::VertId>
HeMesh::edgeVerts(EdgeId e) const noexcept
{
    const auto& edge = m_data->edges[e];
    return {edge.v0, edge.v1};
}

HeMesh::PolyVerts HeMesh::polyVerts(PolyId p) const
{
    PolyVerts out;
    if (!polyValid(p))
        return out;

    HeMeshData::LoopId start = m_data->polyFirstLoop(p);
    if (start == kInvalidLoop)
        return out;

    HeMeshData::LoopId l = start;
    do
    {
        out.push_back(m_data->loopVert(l));
        l = m_data->loopNext(l);
    }
    while (l != start && l != kInvalidLoop);

    return out;
}

HeMesh::PolyEdges HeMesh::polyEdges(PolyId p) const
{
    PolyEdges out;
    if (!polyValid(p))
        return out;

    HeMeshData::LoopId start = m_data->polyFirstLoop(p);
    if (start == kInvalidLoop)
        return out;

    HeMeshData::LoopId l = start;
    do
    {
        out.push_back(m_data->loopEdge(l));
        l = m_data->loopNext(l);
    }
    while (l != start && l != kInvalidLoop);

    return out;
}

HeMesh::PolyLoops HeMesh::polyLoops(PolyId p) const
{
    PolyLoops out;
    if (!polyValid(p))
        return out;

    HeMeshData::LoopId start = m_data->polyFirstLoop(p);
    if (start == kInvalidLoop)
        return out;

    HeMeshData::LoopId l = start;
    do
    {
        out.push_back(l);
        l = m_data->loopNext(l);
    }
    while (l != start && l != kInvalidLoop);

    return out;
}

HeMesh::EdgePolys HeMesh::edgePolys(EdgeId e) const
{
    un::small_list<PolyId, 8> out;
    if (!edgeValid(e))
        return out;

    HeMeshData::LoopId start = m_data->edgeAnyLoop(e);
    if (start == kInvalidLoop)
        return out;

    HeMeshData::LoopId l = start;
    do
    {
        out.insert_unique(m_data->loopPoly(l));
        l = m_data->loopRadialNext(l);
    }
    while (l != start && l != kInvalidLoop);

    return out;
}

HeMesh::VertPolys HeMesh::vertPolys(VertId v) const
{
    VertPolys out;
    if (!vertValid(v))
        return out;

    const auto& loopsAtV = m_data->verts[v].loops;
    for (LoopId l : loopsAtV)
    {
        PolyId p = m_data->loopPoly(l);
        if (polyValid(p))
            out.insert_unique(p);
    }

    return out;
}

HeMesh::VertEdges HeMesh::vertEdges(VertId v) const
{
    VertEdges out;
    if (!vertValid(v))
        return out;

    const auto& loopsAtV = m_data->verts[v].loops;
    for (LoopId l : loopsAtV)
    {
        EdgeId e = m_data->loopEdge(l);
        if (edgeValid(e))
            out.insert_unique(e);
    }

    return out;
}

HeMesh::VertVerts HeMesh::vertVerts(VertId v) const
{
    VertVerts out;
    if (!vertValid(v))
        return out;

    const auto& loopsAtV = m_data->verts[v].loops;
    for (LoopId l : loopsAtV)
    {
        EdgeId e = m_data->loopEdge(l);
        if (!edgeValid(e))
            continue;

        auto [a, b]  = edgeVerts(e);
        VertId other = (a == v) ? b : a;
        if (other != kInvalidVert && vertValid(other))
            out.insert_unique(other);
    }

    return out;
}

// =============================================================
// Edge loop / ring
// =============================================================

std::vector<HeMesh::EdgeId> HeMesh::edgeLoop(EdgeId start) const
{
    std::vector<EdgeId> result;
    if (!edgeValid(start))
        return result;

    // Seed on this edge
    LoopId l0 = m_data->edgeAnyLoop(start);
    if (l0 == kInvalidLoop || !loopValid(l0))
        return result;

    std::unordered_set<EdgeId> visitedEdges;

    auto walkFromLoop = [&](LoopId seed) {
        LoopId l = seed;

        while (true)
        {
            if (!loopValid(l))
                break;

            EdgeId curEdge = m_data->loopEdge(l);
            if (!edgeValid(curEdge))
                break;

            // Stop if we've already seen this edge (loop closed)
            if (!visitedEdges.insert(curEdge).second)
                break;

            result.push_back(curEdge);

            PolyId p = m_data->loopPoly(l);
            if (!polyValid(p))
                break;

            auto loops = polyLoops(p);
            if (loops.size() != 4)
                break; // quads only for now

            int idx = -1;
            for (int i = 0; i < 4; ++i)
            {
                if (loops[static_cast<std::size_t>(i)] == l)
                {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
                break;

            // Opposite edge in the quad
            LoopId lAcross = loops[static_cast<std::size_t>((idx + 2) % 4)];
            EdgeId eAcross = m_data->loopEdge(lAcross);
            if (!edgeValid(eAcross))
                break;

            // Step across to the next polygon sharing that edge
            LoopId r        = m_data->loopRadialNext(lAcross);
            LoopId nextLoop = kInvalidLoop;

            while (r != lAcross && r != kInvalidLoop)
            {
                if (m_data->loopPoly(r) != p)
                {
                    nextLoop = r;
                    break;
                }
                r = m_data->loopRadialNext(r);
            }

            if (nextLoop == kInvalidLoop)
                break;

            l = nextLoop;
        }
    };

    // Walk in one direction from one side
    walkFromLoop(l0);

    // Walk from the "other" side of the starting edge
    LoopId lt = m_data->twinLoop(l0);
    if (lt != kInvalidLoop && loopValid(lt))
        walkFromLoop(lt);

    return result;
}

std::vector<HeMesh::EdgeId> HeMesh::edgeRing(EdgeId start) const
{
    std::vector<EdgeId> result;
    if (!edgeValid(start))
        return result;

    LoopId l0 = m_data->edgeAnyLoop(start);
    if (l0 == kInvalidLoop || !loopValid(l0))
        return result;

    std::unordered_set<EdgeId> visitedEdges;

    auto walkFromLoop = [&](LoopId seed) {
        LoopId l = seed;

        while (true)
        {
            if (!loopValid(l))
                break;

            EdgeId curEdge = m_data->loopEdge(l);
            if (!edgeValid(curEdge))
                break;

            if (!visitedEdges.insert(curEdge).second)
                break; // ring closed or already covered

            result.push_back(curEdge);

            PolyId p = m_data->loopPoly(l);
            if (!polyValid(p))
                break;

            auto loops = polyLoops(p);
            if (loops.size() != 4)
                break; // ring only defined for quads right now

            int idx = -1;
            for (int i = 0; i < 4; ++i)
            {
                if (loops[static_cast<std::size_t>(i)] == l)
                {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
                break;

            // Opposite edge in this quad
            LoopId lAcross = loops[static_cast<std::size_t>((idx + 2) % 4)];
            EdgeId eAcross = m_data->loopEdge(lAcross);
            if (!edgeValid(eAcross))
                break;

            // Find a loop on that opposite edge that belongs to a *different* polygon
            LoopId r          = m_data->loopRadialNext(lAcross);
            LoopId nextLoop   = kInvalidLoop;
            PolyId thisPolyId = p;

            while (r != lAcross && r != kInvalidLoop)
            {
                if (m_data->loopPoly(r) != thisPolyId)
                {
                    nextLoop = r;
                    break;
                }
                r = m_data->loopRadialNext(r);
            }

            if (nextLoop == kInvalidLoop)
                break;

            l = nextLoop;
        }
    };

    // Walk from both sides of the starting edge
    walkFromLoop(l0);

    LoopId lt = m_data->twinLoop(l0);
    if (lt != kInvalidLoop && loopValid(lt))
        walkFromLoop(lt);

    return result;
}

#include <iostream>

void HeMesh::debugPrint() const
{
    using std::cerr;
    using std::endl;

    cerr << "\n=============================\n";
    cerr << "         HEMesh Dump         \n";
    cerr << "=============================\n";

    auto capVerts = m_data->verts.capacity();
    auto capEdges = m_data->edges.capacity();
    auto capPolys = m_data->polys.capacity();
    auto capLoops = m_data->loops.capacity();

    cerr << "Verts: " << vertCount() << " (capacity: " << capVerts << ")\n";
    cerr << "Edges: " << edgeCount() << " (capacity: " << capEdges << ")\n";
    cerr << "Polys: " << polyCount() << " (capacity: " << capPolys << ")\n";
    cerr << "Loops: " << loopCount() << " (capacity: " << capLoops << ")\n";

    cerr << "------------------------------------\n";
    cerr << " Live vs dead slots...\n";

    int deadVerts = 0, deadEdges = 0, deadPolys = 0, deadLoops = 0;

    for (VertId v = 0; v < capVerts; ++v)
        if (!vertValid(v))
            ++deadVerts;
    for (EdgeId e = 0; e < capEdges; ++e)
        if (!edgeValid(e))
            ++deadEdges;
    for (PolyId p = 0; p < capPolys; ++p)
        if (!polyValid(p))
            ++deadPolys;
    for (LoopId l = 0; l < capLoops; ++l)
        if (!loopValid(l))
            ++deadLoops;

    cerr << "Dead verts (removed slots): " << deadVerts << endl;
    cerr << "Dead edges (removed slots): " << deadEdges << endl;
    cerr << "Dead polys (removed slots): " << deadPolys << endl;
    cerr << "Dead loops (removed slots): " << deadLoops << endl;

    // ------------------------------------------------------------
    // validate() – full integrity check
    // ------------------------------------------------------------
    cerr << "\nRunning invariant check (validate())...\n";
    bool ok = validate();
    cerr << "validate() result: " << (ok ? "OK (no structural errors)" : "ERRORS FOUND") << "\n";

    // ------------------------------------------------------------
    // Dump first 10 polygons
    // ------------------------------------------------------------
    cerr << "\nDumping first 10 *valid* polygons...\n";

    int dumped = 0;
    for (PolyId p = 0; p < polyCount() && dumped < 10; ++p)
    {
        if (!polyValid(p))
            continue;

        ++dumped;

        cerr << "Poly " << p << ": material=" << polyMaterial(p) << "  verts:[ ";
        for (VertId v : polyVerts(p))
            cerr << v << " ";
        cerr << "] edges:[ ";
        for (EdgeId e : polyEdges(p))
        {
            auto ev = edgeVerts(e);
            cerr << "(" << ev.first << "," << ev.second << ") ";
        }
        cerr << "]\n";
    }

    cerr << "\n=== DONE debugPrint ===\n\n";
}

void dumpEdgeLoop(const HeMesh& mesh, HeMesh::EdgeId start)
{
    std::cerr << "\n=== EdgeLoop from edge " << start << " ===\n";
    auto loop = mesh.edgeLoop(start);
    std::cerr << "Loop size: " << loop.size() << "\n";
    for (auto e : loop)
    {
        auto ev = mesh.edgeVerts(e);
        std::cerr << "  Edge " << e << " : (" << ev.first << ", " << ev.second << ")\n";
    }
}

bool HeMesh::validate() const
{
    using std::cerr;
    using std::endl;

    bool ok = true;

    cerr << "\n=============================\n";
    cerr << "       HEMesh::validate      \n";
    cerr << "=============================\n";

    // Use capacities, not counts, to scan all possible IDs.
    const int vSlots = static_cast<int>(m_data->verts.capacity());
    const int eSlots = static_cast<int>(m_data->edges.capacity());
    const int pSlots = static_cast<int>(m_data->polys.capacity());
    const int lSlots = static_cast<int>(m_data->loops.capacity());

    int liveVerts = 0, deadVerts = 0;
    int liveEdges = 0, deadEdges = 0;
    int livePolys = 0, deadPolys = 0;
    int liveLoops = 0, deadLoops = 0;

    for (VertId v = 0; v < vSlots; ++v)
    {
        if (vertValid(v))
            ++liveVerts;
        else
            ++deadVerts;
    }

    for (EdgeId e = 0; e < eSlots; ++e)
    {
        if (edgeValid(e))
            ++liveEdges;
        else
            ++deadEdges;
    }

    for (PolyId p = 0; p < pSlots; ++p)
    {
        if (polyValid(p))
            ++livePolys;
        else
            ++deadPolys;
    }

    for (LoopId l = 0; l < lSlots; ++l)
    {
        if (loopValid(l))
            ++liveLoops;
        else
            ++deadLoops;
    }

    cerr << "Vert slots:  " << vSlots << " (live: " << liveVerts << ", invalid: " << deadVerts << ")\n";
    cerr << "Edge slots:  " << eSlots << " (live: " << liveEdges << ", invalid: " << deadEdges << ")\n";
    cerr << "Poly  slots: " << pSlots << " (live: " << livePolys << ", invalid: " << deadPolys << ")\n";
    cerr << "Loop  slots: " << lSlots << " (live: " << liveLoops << ", invalid: " << deadLoops << ")\n";

    // Holes are fine – we only treat *structural* problems as errors.

    // Holes are fine – we only treat *structural* problems as errors.

    // ------------------------------------------------------------
    // 1) Loop → vert/edge/poly references
    // ------------------------------------------------------------
    cerr << "\n[1] Loop → vert/edge/poly checks...\n";

    int loopsBadVert = 0;
    int loopsBadEdge = 0;
    int loopsBadPoly = 0;

    for (LoopId l = 0; l < lSlots; ++l)
    {
        if (!loopValid(l))
            continue;

        VertId v = m_data->loopVert(l);
        EdgeId e = m_data->loopEdge(l);
        PolyId p = m_data->loopPoly(l);

        if (!vertValid(v))
        {
            ++loopsBadVert;
            ok = false;
            cerr << "  ERROR: loop " << l << " has INVALID vert " << v << "\n";
        }
        if (!edgeValid(e))
        {
            ++loopsBadEdge;
            ok = false;
            cerr << "  ERROR: loop " << l << " has INVALID edge " << e << "\n";
        }
        if (!polyValid(p))
        {
            ++loopsBadPoly;
            ok = false;
            cerr << "  ERROR: loop " << l << " has INVALID poly " << p << "\n";
        }
    }

    cerr << "  Loops with invalid vert: " << loopsBadVert << "\n";
    cerr << "  Loops with invalid edge: " << loopsBadEdge << "\n";
    cerr << "  Loops with invalid poly: " << loopsBadPoly << "\n";

    // ------------------------------------------------------------
    // 2) Polygon loop rings (next/prev + loopPoly)
    // ------------------------------------------------------------
    cerr << "\n[2] Polygon loop rings...\n";

    int polysNoLoops   = 0;
    int polysBadNext   = 0;
    int polysBadPrev   = 0;
    int polysWrongPoly = 0;

    for (PolyId p = 0; p < pSlots; ++p)
    {
        if (!polyValid(p))
            continue;

        auto loops = polyLoops(p); // all loops around this poly

        if (loops.empty())
        {
            ++polysNoLoops;
            ok = false;
            cerr << "  ERROR: poly " << p << " has NO loops\n";
            continue;
        }

        const int n = static_cast<int>(loops.size());
        for (int i = 0; i < n; ++i)
        {
            LoopId l  = loops[i];
            LoopId nx = m_data->loopNext(l);
            LoopId pr = m_data->loopPrev(l);

            LoopId expectedN = loops[(i + 1) % n];
            LoopId expectedP = loops[(i + n - 1) % n];

            if (nx != expectedN)
            {
                ++polysBadNext;
                ok = false;
                cerr << "  ERROR: poly " << p << " loop " << l
                     << " NEXT=" << nx << " expected " << expectedN << "\n";
            }
            if (pr != expectedP)
            {
                ++polysBadPrev;
                ok = false;
                cerr << "  ERROR: poly " << p << " loop " << l
                     << " PREV=" << pr << " expected " << expectedP << "\n";
            }

            PolyId lp = m_data->loopPoly(l);
            if (lp != p)
            {
                ++polysWrongPoly;
                ok = false;
                cerr << "  ERROR: poly " << p << " loop " << l
                     << " reports loopPoly=" << lp << "\n";
            }
        }
    }

    cerr << "  Polys with NO loops:       " << polysNoLoops << "\n";
    cerr << "  Polys with bad NEXT links: " << polysBadNext << "\n";
    cerr << "  Polys with bad PREV links: " << polysBadPrev << "\n";
    cerr << "  Polys with wrong loopPoly: " << polysWrongPoly << "\n";

    // ------------------------------------------------------------
    // 3) Edge radial fans (edgeAnyLoop + loopRadialNext ring)
    // ------------------------------------------------------------
    cerr << "\n[3] Edge radial fans...\n";

    int edgesNoRadial    = 0;
    int edgesBadRingEdge = 0;
    int edgesBadFanCycle = 0;

    for (EdgeId e = 0; e < eSlots; ++e)
    {
        if (!edgeValid(e))
            continue;

        LoopId start = m_data->edgeAnyLoop(e);
        if (start == kInvalidLoop)
        {
            ++edgesNoRadial;
            ok = false;
            cerr << "  ERROR: edge " << e
                 << " has NO radial loop reference (edgeAnyLoop == kInvalidLoop)\n";
            continue;
        }

        if (!loopValid(start))
        {
            ++edgesBadFanCycle;
            ok = false;
            cerr << "  ERROR: edge " << e << " edgeAnyLoop=" << start
                 << " but that loop is INVALID\n";
            continue;
        }

        std::unordered_set<LoopId> seen;
        LoopId                     l     = start;
        int                        steps = 0;
        const int                  guard = lSlots + 1;

        while (true)
        {
            if (!loopValid(l))
            {
                ++edgesBadFanCycle;
                ok = false;
                cerr << "  ERROR: edge " << e
                     << " radial ring hit INVALID loop " << l << "\n";
                break;
            }

            if (m_data->loopEdge(l) != e)
            {
                ++edgesBadRingEdge;
                ok = false;
                cerr << "  ERROR: edge " << e << " radial loop " << l
                     << " has loopEdge=" << m_data->loopEdge(l)
                     << " (expected " << e << ")\n";
            }

            if (!seen.insert(l).second)
            {
                if (l == start && steps > 0)
                {
                    // closed cleanly
                }
                else
                {
                    ++edgesBadFanCycle;
                    ok = false;
                    cerr << "  ERROR: edge " << e
                         << " radial ring has a cycle not starting at edgeAnyLoop\n";
                }
                break;
            }

            ++steps;
            if (steps > guard)
            {
                ++edgesBadFanCycle;
                ok = false;
                cerr << "  ERROR: edge " << e
                     << " radial ring did not close within guard (" << guard << " steps)\n";
                break;
            }

            l = m_data->loopRadialNext(l);
            if (l == start)
                break; // normal closure

            if (l == kInvalidLoop)
            {
                ++edgesBadFanCycle;
                ok = false;
                cerr << "  ERROR: edge " << e
                     << " radial ring hit kInvalidLoop before closing\n";
                break;
            }
        }
    }

    cerr << "  Edges with NO radial fan:      " << edgesNoRadial << "\n";
    cerr << "  Edges with wrong loopEdge():   " << edgesBadRingEdge << "\n";
    cerr << "  Edges with bad radial cycles:  " << edgesBadFanCycle << "\n";

    // ------------------------------------------------------------
    // Final verdict
    // ------------------------------------------------------------
    cerr << "\nHEMesh::validate() => " << (ok ? "OK" : "ERRORS FOUND") << "\n\n";
    return ok;
}

void HeMesh::setPolyVerts(PolyId p, const std::vector<VertId>& verts)
{
    if (!polyValid(p))
        return;

    auto& d = *m_data;

    HeMeshData::Poly&  poly  = d.polys[p];
    HeMeshData::LoopId start = poly.firstLoop;

    // --------------------------------------------------------
    // 1) Collect old loops and detach them from everything
    // --------------------------------------------------------
    std::vector<HeMeshData::LoopId> oldLoops;
    if (start != HeMeshData::kInvalidLoop)
    {
        HeMeshData::LoopId l = start;
        do
        {
            oldLoops.push_back(l);
            l = d.loopNext(l);
        }
        while (l != start && l != HeMeshData::kInvalidLoop);
    }

    for (HeMeshData::LoopId l : oldLoops)
    {
        HeMeshData::VertId v = d.loopVert(l);
        HeMeshData::EdgeId e = d.loopEdge(l);

        // detach from vertex fan
        d.removeLoopFromVert(v, l);

        // detach from edge radial fan
        if (edgeValid(e))
            d.detachLoopFromEdge(e, l);

        d.loops[l].removed = true;
        d.loops.remove(l);
    }

    poly.firstLoop = HeMeshData::kInvalidLoop;

    // --------------------------------------------------------
    // 2) Build new loops
    // --------------------------------------------------------
    const int n = static_cast<int>(verts.size());
    if (n < 3)
        return;

    std::vector<HeMeshData::LoopId> newLoops;
    newLoops.reserve(n);

    for (int i = 0; i < n; ++i)
    {
        VertId v0 = verts[i];
        VertId v1 = verts[(i + 1) % n];

        if (!vertValid(v0) || !vertValid(v1))
            return;

        EdgeId e = ensureEdge(v0, v1);
        if (!edgeValid(e))
            return;

        HeMeshData::LoopId l = d.createLoop(p, v0, e);
        newLoops.push_back(l);

        // Radial attach
        HeMeshData::LoopId any = d.edgeAnyLoop(e);
        if (any == HeMeshData::kInvalidLoop)
        {
            d.setEdgeAnyLoop(e, l);
            d.loops[l].radialPrev = l;
            d.loops[l].radialNext = l;
        }
        else
        {
            HeMeshData::LoopId a = any;
            HeMeshData::LoopId b = d.loopRadialNext(a);

            d.loops[l].radialPrev = a;
            d.loops[l].radialNext = b;
            d.loops[a].radialNext = l;
            d.loops[b].radialPrev = l;
        }
    }

    // --------------------------------------------------------
    // 3) Close polygon ring (next/prev)
    // --------------------------------------------------------
    for (int i = 0; i < n; ++i)
    {
        HeMeshData::LoopId l   = newLoops[i];
        HeMeshData::LoopId nxt = newLoops[(i + 1) % n];
        HeMeshData::LoopId prv = newLoops[(i + n - 1) % n];
        d.setLoopNextPrev(l, nxt, prv);
    }

    poly.firstLoop = newLoops.front();
}

bool HeMesh::validateLoopsAndRings(int32_t maxDebugEdges) const
{
    using std::cerr;
    using std::endl;

    bool ok = true;

    cerr << "\n=============================\n";
    cerr << "  HEMesh::validateLoopsAndRings\n";
    cerr << "=============================\n";

    auto edges = allEdges();
    cerr << "Total valid edges: " << edges.size() << "\n";

    auto checkStrip = [&](const char*                label,
                          EdgeId                     startEdge,
                          const std::vector<EdgeId>& strip,
                          bool                       isLoop) {
        if (strip.empty())
        {
            // Empty strips are allowed, but maybe suspicious if mesh is
            // supposed to be mostly quad-strippable.
            return;
        }

        // 1) All edges valid & unique
        std::unordered_set<EdgeId> seen;
        for (EdgeId e : strip)
        {
            if (!edgeValid(e))
            {
                ok = false;
                cerr << "  ERROR: " << label
                     << " from edge " << startEdge
                     << " contains INVALID edge id " << e << "\n";
            }
            if (!seen.insert(e).second)
            {
                ok = false;
                cerr << "  ERROR: " << label
                     << " from edge " << startEdge
                     << " has DUPLICATE edge id " << e << "\n";
            }
        }

        // 2) Local adjacency checks:
        //    - For loops: consecutive edges should share a vertex.
        //    - For rings: consecutive edges should share a polygon.
        for (size_t i = 0; i + 1 < strip.size(); ++i)
        {
            EdgeId e0 = strip[i];
            EdgeId e1 = strip[i + 1];

            if (!edgeValid(e0) || !edgeValid(e1))
                continue;

            auto [a0, b0] = edgeVerts(e0);
            auto [a1, b1] = edgeVerts(e1);

            if (isLoop)
            {
                // Loop: should share at least one vertex
                bool shareVert = (a0 == a1) || (a0 == b1) || (b0 == a1) || (b0 == b1);
                if (!shareVert)
                {
                    ok = false;
                    cerr << "  ERROR: " << label
                         << " from edge " << startEdge
                         << " has NON-VERT-ADJACENT pair: e"
                         << e0 << " (" << a0 << "," << b0 << "), e"
                         << e1 << " (" << a1 << "," << b1 << ")\n";
                }
            }
            else
            {
                // Ring: should share at least one polygon
                auto polys0 = edgePolys(e0);
                auto polys1 = edgePolys(e1);

                bool sharePoly = false;
                for (PolyId p0 : polys0)
                {
                    for (PolyId p1 : polys1)
                    {
                        if (p0 == p1 && polyValid(p0))
                        {
                            sharePoly = true;
                            break;
                        }
                    }
                    if (sharePoly)
                        break;
                }

                if (!sharePoly)
                {
                    ok = false;
                    cerr << "  ERROR: " << label
                         << " from edge " << startEdge
                         << " has NON-POLY-ADJACENT pair: e"
                         << e0 << " and e" << e1 << "\n";
                }
            }
        }
    };

    int debugCount = 0;

    for (EdgeId e : edges)
    {
        if (!edgeValid(e))
            continue;

        auto loop = edgeLoop(e);
        auto ring = edgeRing(e);

        if (debugCount < maxDebugEdges)
        {
            cerr << "\nEdge " << e << ":\n";
            cerr << "  loop size = " << loop.size() << "\n";
            cerr << "  ring size = " << ring.size() << "\n";

            cerr << "  loop edges: ";
            for (EdgeId le : loop)
                cerr << le << " ";
            cerr << "\n";

            cerr << "  ring edges: ";
            for (EdgeId re : ring)
                cerr << re << " ";
            cerr << "\n";

            ++debugCount;
        }

        checkStrip("edgeLoop", e, loop, /*isLoop*/ false);
        checkStrip("edgeRing", e, ring, /*isLoop*/ false);
    }

    cerr << "\nHEMesh::validateLoopsAndRings() => "
         << (ok ? "OK" : "ERRORS FOUND") << "\n\n";

    return ok;
}
