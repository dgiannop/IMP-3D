#include "HalfEdgeView.hpp"

#include <SysMesh.hpp>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------

namespace
{
    static uint64_t packDirectedKey(int32_t a, int32_t b) noexcept
    {
        const uint64_t ua = static_cast<uint64_t>(static_cast<uint32_t>(a));
        const uint64_t ub = static_cast<uint64_t>(static_cast<uint32_t>(b));
        return (ua << 32ull) | ub;
    }

    static uint64_t packUndirectedKey(int32_t a, int32_t b) noexcept
    {
        if (a > b)
            std::swap(a, b);

        return packDirectedKey(a, b);
    }

    static HalfEdgeView::EdgeKey normalizeEdgeKey(HalfEdgeView::VertId a,
                                                  HalfEdgeView::VertId b) noexcept
    {
        if (a > b)
            std::swap(a, b);

        return {a, b};
    }
} // namespace

// ---------------------------------------------------------
// Lookup (private impl)
// ---------------------------------------------------------

struct HalfEdgeView::Lookup
{
    std::unordered_map<uint64_t, LoopId>              dirToLoop;
    std::unordered_map<uint64_t, std::vector<LoopId>> edgeToLoops;
};

// ---------------------------------------------------------
// Build / lifetime
// ---------------------------------------------------------

HalfEdgeView::HalfEdgeView()  = default;
HalfEdgeView::~HalfEdgeView() = default;

void HalfEdgeView::clear() noexcept
{
    m_loopFrom.clear();
    m_loopTo.clear();
    m_loopPoly.clear();
    m_loopNext.clear();
    m_loopPrev.clear();
    m_loopTwin.clear();
    m_loopCorner.clear();
    m_polyFirstLoop.clear();
    m_lookup.reset();
}

bool HalfEdgeView::empty() const noexcept
{
    return m_loopFrom.empty();
}

int32_t HalfEdgeView::loopCount() const noexcept
{
    return static_cast<int32_t>(m_loopFrom.size());
}

void HalfEdgeView::build(const SysMesh* mesh)
{
    clear();

    if (!mesh)
        return;

    m_lookup = std::make_unique<Lookup>();

    // ---------------------------------------------------------
    // Determine max PolyId for dense polyFirstLoop storage
    // ---------------------------------------------------------
    int32_t maxPid = -1;
    for (const int32_t pid : mesh->all_polys())
        maxPid = std::max(maxPid, pid);

    if (maxPid < 0)
        return;

    m_polyFirstLoop.assign(static_cast<size_t>(maxPid + 1), kInvalidLoop);

    // ---------------------------------------------------------
    // Pass 1: create loops + per-poly loopNext/loopPrev rings
    // ---------------------------------------------------------
    for (const int32_t pid : mesh->all_polys())
    {
        if (!mesh->poly_valid(pid))
            continue;

        const auto    verts = mesh->poly_verts(pid);
        const int32_t n     = static_cast<int32_t>(verts.size());

        if (n < 3)
            continue;

        const LoopId first                        = static_cast<LoopId>(m_loopFrom.size());
        m_polyFirstLoop[static_cast<size_t>(pid)] = first;

        for (int32_t i = 0; i < n; ++i)
        {
            const VertId a = static_cast<VertId>(verts[static_cast<size_t>(i)]);
            const VertId b = static_cast<VertId>(verts[static_cast<size_t>((i + 1) % n)]);

            const LoopId l = static_cast<LoopId>(m_loopFrom.size());

            m_loopFrom.push_back(a);
            m_loopTo.push_back(b);
            m_loopPoly.push_back(pid);
            m_loopNext.push_back(kInvalidLoop);
            m_loopPrev.push_back(kInvalidLoop);
            m_loopTwin.push_back(kInvalidLoop);

            // Corner index in the polygon ring (aligned to SysMesh::poly_verts and map_poly_verts).
            m_loopCorner.push_back(i);

            // Directed lookup (a -> b)
            const uint64_t dkey = packDirectedKey(a, b);
            m_lookup->dirToLoop.try_emplace(dkey, l);

            // Undirected edge bucket
            const uint64_t ukey = packUndirectedKey(a, b);
            m_lookup->edgeToLoops[ukey].push_back(l);
        }

        // Patch loopNext/loopPrev cycle
        for (int32_t i = 0; i < n; ++i)
        {
            const LoopId cur = first + i;

            const LoopId next = first + ((i + 1) % n);
            const LoopId prev = first + ((i - 1 + n) % n);

            m_loopNext[static_cast<size_t>(cur)] = next;
            m_loopPrev[static_cast<size_t>(cur)] = prev;
        }
    }

    // ---------------------------------------------------------
    // Pass 2: twin pairing
    // ---------------------------------------------------------
    for (auto& it : m_lookup->edgeToLoops)
    {
        const std::vector<LoopId>& loops = it.second;

        if (loops.size() != 2)
        {
            // boundary or non-manifold
            for (const LoopId l : loops)
                m_loopTwin[static_cast<size_t>(l)] = kInvalidLoop;
            continue;
        }

        const LoopId l0 = loops[0];
        const LoopId l1 = loops[1];

        const VertId a0 = m_loopFrom[static_cast<size_t>(l0)];
        const VertId b0 = m_loopTo[static_cast<size_t>(l0)];
        const VertId a1 = m_loopFrom[static_cast<size_t>(l1)];
        const VertId b1 = m_loopTo[static_cast<size_t>(l1)];

        const bool opposite = (a0 == b1) && (b0 == a1);

        if (!opposite)
        {
            m_loopTwin[static_cast<size_t>(l0)] = kInvalidLoop;
            m_loopTwin[static_cast<size_t>(l1)] = kInvalidLoop;
            continue;
        }

        m_loopTwin[static_cast<size_t>(l0)] = l1;
        m_loopTwin[static_cast<size_t>(l1)] = l0;
    }
}

// ---------------------------------------------------------
// Loop core accessors
// ---------------------------------------------------------

bool HalfEdgeView::loopValid(LoopId l) const noexcept
{
    return l >= 0 && static_cast<size_t>(l) < m_loopFrom.size();
}

HalfEdgeView::VertId HalfEdgeView::loopFrom(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopFrom[static_cast<size_t>(l)] : kInvalidVert;
}

HalfEdgeView::VertId HalfEdgeView::loopTo(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopTo[static_cast<size_t>(l)] : kInvalidVert;
}

HalfEdgeView::PolyId HalfEdgeView::loopPoly(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopPoly[static_cast<size_t>(l)] : kInvalidPoly;
}

HalfEdgeView::LoopId HalfEdgeView::loopNext(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopNext[static_cast<size_t>(l)] : kInvalidLoop;
}

HalfEdgeView::LoopId HalfEdgeView::loopPrev(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopPrev[static_cast<size_t>(l)] : kInvalidLoop;
}

HalfEdgeView::LoopId HalfEdgeView::loopTwin(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopTwin[static_cast<size_t>(l)] : kInvalidLoop;
}

int32_t HalfEdgeView::loopCorner(LoopId l) const noexcept
{
    return loopValid(l) ? m_loopCorner[static_cast<size_t>(l)] : -1;
}

HalfEdgeView::EdgeKey HalfEdgeView::loopEdgeKey(LoopId l) const noexcept
{
    if (!loopValid(l))
        return {kInvalidVert, kInvalidVert};

    return normalizeEdgeKey(m_loopFrom[static_cast<size_t>(l)],
                            m_loopTo[static_cast<size_t>(l)]);
}

// ---------------------------------------------------------
// Polygon traversal
// ---------------------------------------------------------

bool HalfEdgeView::polyValid(PolyId p) const noexcept
{
    return p >= 0 && static_cast<size_t>(p) < m_polyFirstLoop.size() &&
           m_polyFirstLoop[static_cast<size_t>(p)] != kInvalidLoop;
}

HalfEdgeView::LoopId HalfEdgeView::polyFirstLoop(PolyId p) const noexcept
{
    return polyValid(p) ? m_polyFirstLoop[static_cast<size_t>(p)] : kInvalidLoop;
}

HalfEdgeView::PolyLoops HalfEdgeView::polyLoops(PolyId p) const
{
    PolyLoops out = {};

    const LoopId first = polyFirstLoop(p);
    if (first == kInvalidLoop)
        return out;

    LoopId cur = first;

    for (int32_t guard = 0; guard < 1024; ++guard)
    {
        out.push_back(cur);

        cur = loopNext(cur);
        if (cur == kInvalidLoop || cur == first)
            break;
    }

    return out;
}

HalfEdgeView::PolyVerts HalfEdgeView::polyVerts(PolyId p) const
{
    PolyVerts out = {};

    const PolyLoops loops = polyLoops(p);
    for (const LoopId l : loops)
        out.push_back(loopFrom(l));

    return out;
}

HalfEdgeView::PolyPolys HalfEdgeView::polyNeighborPolys(PolyId p) const
{
    PolyPolys out = {};

    const PolyLoops loops = polyLoops(p);
    for (const LoopId l : loops)
    {
        const LoopId t = loopTwin(l);
        out.push_back(t != kInvalidLoop ? loopPoly(t) : kInvalidPoly);
    }

    return out;
}

// ---------------------------------------------------------
// Tool helpers
// ---------------------------------------------------------

HalfEdgeView::LoopId HalfEdgeView::findLoop(VertId a, VertId b) const noexcept
{
    if (!m_lookup)
        return kInvalidLoop;

    const uint64_t key = packDirectedKey(a, b);

    const auto it = m_lookup->dirToLoop.find(key);
    if (it == m_lookup->dirToLoop.end())
        return kInvalidLoop;

    return it->second;
}

HalfEdgeView::LoopId HalfEdgeView::findEdge(VertId a, VertId b) const noexcept
{
    if (!m_lookup)
        return kInvalidLoop;

    const uint64_t key = packUndirectedKey(a, b);

    const auto it = m_lookup->edgeToLoops.find(key);
    if (it == m_lookup->edgeToLoops.end() || it->second.empty())
        return kInvalidLoop;

    return it->second.front();
}

std::vector<HalfEdgeView::PolyId> HalfEdgeView::edgePolys(VertId a, VertId b) const
{
    std::vector<PolyId> out = {};

    if (!m_lookup)
        return out;

    const uint64_t key = packUndirectedKey(a, b);

    const auto it = m_lookup->edgeToLoops.find(key);
    if (it == m_lookup->edgeToLoops.end())
        return out;

    for (const LoopId l : it->second)
    {
        const PolyId p = loopPoly(l);
        if (p == kInvalidPoly)
            continue;

        if (std::find(out.begin(), out.end(), p) == out.end())
            out.push_back(p);
    }

    return out;
}

std::vector<HalfEdgeView::EdgeKey> HalfEdgeView::edgeRing(VertId a, VertId b) const
{
    std::vector<EdgeKey> out = {};

    if (!m_lookup)
        return out;

    const EdgeKey  startKey = normalizeEdgeKey(a, b);
    const uint64_t startU   = packUndirectedKey(startKey.first, startKey.second);

    const auto it = m_lookup->edgeToLoops.find(startU);
    if (it == m_lookup->edgeToLoops.end() || it->second.empty())
        return out;

    auto edgeLoopInPoly = [&](PolyId p) -> LoopId {
        for (const LoopId l : it->second)
        {
            if (loopPoly(l) == p)
                return l;
        }
        return kInvalidLoop;
    };

    auto walkFromPoly = [&](PolyId startPoly) -> std::vector<EdgeKey> {
        std::vector<EdgeKey> seq = {};

        LoopId cur = edgeLoopInPoly(startPoly);
        if (cur == kInvalidLoop)
            return seq;

        std::unordered_map<uint64_t, bool> seen = {};
        seen[startU]                            = true;

        for (int32_t guard = 0; guard < 4096; ++guard)
        {
            const PolyId p = loopPoly(cur);
            if (p == kInvalidPoly)
                break;

            const PolyLoops loops = polyLoops(p);
            if (loops.size() != 4)
                break;

            int32_t idx = -1;
            for (int32_t i = 0; i < 4; ++i)
            {
                if (loops[static_cast<size_t>(i)] == cur)
                {
                    idx = i;
                    break;
                }
            }

            if (idx < 0)
                break;

            const LoopId  opp     = loops[static_cast<size_t>((idx + 2) % 4)];
            const EdgeKey nextKey = loopEdgeKey(opp);

            const uint64_t nextU = packUndirectedKey(nextKey.first, nextKey.second);
            if (seen.contains(nextU))
                break;

            seen[nextU] = true;
            seq.push_back(nextKey);

            const LoopId t = loopTwin(opp);
            if (t == kInvalidLoop)
                break;

            cur = t;
        }

        return seq;
    };

    out.push_back(startKey);

    const std::vector<PolyId> polys = edgePolys(startKey.first, startKey.second);

    if (polys.size() == 1)
    {
        const auto fwd = walkFromPoly(polys[0]);
        out.insert(out.end(), fwd.begin(), fwd.end());
        return out;
    }

    if (polys.size() >= 2)
    {
        const auto aSide = walkFromPoly(polys[0]);
        const auto bSide = walkFromPoly(polys[1]);

        for (auto itB = bSide.rbegin(); itB != bSide.rend(); ++itB)
            out.insert(out.begin(), *itB);

        out.insert(out.end(), aSide.begin(), aSide.end());
    }

    return out;
}

// ---------------------------------------------------------
// HalfEdgeView::edgeLoop (REAL edge loop, quad-manifold)
// ---------------------------------------------------------

std::vector<HalfEdgeView::EdgeKey> HalfEdgeView::edgeLoop(VertId a, VertId b) const
{
    std::vector<EdgeKey> result = {};

    if (!m_lookup)
        return result;

    const LoopId seedAny = findEdge(a, b);
    if (seedAny == kInvalidLoop || !loopValid(seedAny))
        return result;

    auto packEdgeKey = [](const EdgeKey& e) noexcept -> uint64_t {
        const uint64_t ua = static_cast<uint64_t>(static_cast<uint32_t>(e.first));
        const uint64_t ub = static_cast<uint64_t>(static_cast<uint32_t>(e.second));
        return (ua << 32ull) | ub;
    };

    // next outgoing loop around the ORIGIN vertex of l (standard half-edge op)
    // This is: twin(prev(l)) around the origin vertex of l.
    auto vertNext = [&](LoopId l) -> LoopId {
        const LoopId prev = loopPrev(l);
        if (prev == kInvalidLoop)
            return kInvalidLoop;

        return loopTwin(prev); // twin(prev(l))
    };

    // Measure valence by cycling vertNext around origin of 'start'
    auto vertValence = [&](LoopId start) -> int32_t {
        if (start == kInvalidLoop || !loopValid(start))
            return 0;

        LoopId  cur = start;
        int32_t n   = 0;

        for (int32_t guard = 0; guard < 64; ++guard)
        {
            ++n;
            cur = vertNext(cur);
            if (cur == kInvalidLoop)
                return 0; // non-manifold/boundary breaks the vertex fan
            if (cur == start)
                break;
        }

        return n;
    };

    // Step through the "to" vertex of l = (a->b):
    // take reversed half-edge at b, then move 2 steps around vertex to go "straight".
    auto stepForward = [&](LoopId l) -> LoopId {
        const LoopId lRev = loopTwin(l);
        if (lRev == kInvalidLoop)
            return kInvalidLoop;

        // Only do the quad-ish "straight through vertex" rule on valence-4 vertices
        if (vertValence(lRev) != 4)
            return kInvalidLoop;

        const LoopId h1 = vertNext(lRev);
        if (h1 == kInvalidLoop)
            return kInvalidLoop;

        const LoopId h2 = vertNext(h1);
        if (h2 == kInvalidLoop)
            return kInvalidLoop;

        return h2; // outgoing from the vertex, represents the next edge in the loop
    };

    // Walk one direction collecting undirected edges (first element is the starting edge)
    auto walk = [&](LoopId start) -> std::vector<EdgeKey> {
        std::vector<EdgeKey> out = {};

        LoopId cur = start;

        for (int32_t guard = 0; guard < 4096; ++guard)
        {
            if (!loopValid(cur))
                break;

            const EdgeKey ekey = loopEdgeKey(cur);
            if (ekey.first == kInvalidVert || ekey.second == kInvalidVert)
                break;

            out.push_back(ekey);

            const LoopId next = stepForward(cur);
            if (next == kInvalidLoop)
                break;

            cur = next;
        }

        return out;
    };

    // Seed edge (normalized)
    const EdgeKey seedKey = loopEdgeKey(seedAny);
    if (seedKey.first == kInvalidVert || seedKey.second == kInvalidVert)
        return result;

    // Uniqueness across the combined path
    std::unordered_set<uint64_t> visited = {};
    visited.reserve(256);

    auto addUnique = [&](std::vector<EdgeKey>& dst, const EdgeKey& e) -> bool {
        const uint64_t k = packEdgeKey(e);
        if (!visited.insert(k).second)
            return false;
        dst.push_back(e);
        return true;
    };

    visited.insert(packEdgeKey(seedKey));

    // Forward direction from seedAny
    std::vector<EdgeKey> forward = {};
    {
        const std::vector<EdgeKey> seq = walk(seedAny);
        for (size_t i = 1; i < seq.size(); ++i) // skip seed
            addUnique(forward, seq[i]);
    }

    // Backward direction: walk forward from the twin of the seed (reverses direction)
    std::vector<EdgeKey> backward = {};
    {
        const LoopId seedTwin = loopTwin(seedAny);
        if (seedTwin != kInvalidLoop && loopValid(seedTwin))
        {
            const std::vector<EdgeKey> seq = walk(seedTwin);
            for (size_t i = 1; i < seq.size(); ++i) // skip seed
                addUnique(backward, seq[i]);
        }
    }

    // Stitch: reverse(backward) + seed + forward
    result.clear();
    result.reserve(backward.size() + 1 + forward.size());

    for (auto it = backward.rbegin(); it != backward.rend(); ++it)
        result.push_back(*it);

    result.push_back(seedKey);

    result.insert(result.end(), forward.begin(), forward.end());

    return result;
}
