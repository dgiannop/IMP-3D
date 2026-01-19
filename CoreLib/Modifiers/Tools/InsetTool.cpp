#include "InsetTool.hpp"

#include <SysMesh.hpp>
#include <vector>

#include "CoreUtilities.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

InsetTool::InsetTool()
{
    addProperty("Amount", PropertyType::FLOAT, &m_amount, 0.f);
    addProperty("Group polygons", PropertyType::BOOL, &m_group);
}

void InsetTool::activate(Scene* /*scene*/)
{
}

void InsetTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_amount))
        return;

    auto polyMap = sel::to_polys(scene);

    for (auto& [mesh, polys] : polyMap)
    {
        if (!mesh)
            continue;

        InsetTool::insetPolys(mesh, polys, m_amount, m_group);
    }
}

void InsetTool::mouseDown(Viewport*, Scene*, const CoreEvent&)
{
}

void InsetTool::mouseDrag(Viewport* vp, Scene* /*scene*/, const CoreEvent& event)
{
    m_amount += event.deltaX * vp->pixelScale();
    m_amount += event.deltaY * vp->pixelScale();
}

void InsetTool::mouseUp(Viewport*, Scene*, const CoreEvent&)
{
}

void InsetTool::render(Viewport*, Scene*)
{
}

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "HeMeshBridge.hpp"
#include "InsetTool.hpp"
#include "SysMesh.hpp"

// ------------------------------------------------------------
// Helpers (local)
// ------------------------------------------------------------
namespace
{
    static glm::vec3 safe_normalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
    {
        const float len2 = glm::dot(v, v);
        if (len2 <= 1e-20f)
            return fallback;
        return v * (1.0f / std::sqrt(len2));
    }

    static glm::vec3 project_to_plane(const glm::vec3& v, const glm::vec3& nUnit) noexcept
    {
        return v - nUnit * glm::dot(v, nUnit);
    }

    /**
     * @brief Compute inset delta for a boundary vertex using its prev/next boundary neighbors.
     *
     * This is the standard miter inset in the local tangent plane of nUnit.
     * Works well for planar-ish regions; for highly curved patches it's still stable
     * because nUnit is computed as a local average from editable polys.
     */
    static glm::vec3 compute_boundary_inset_delta(const glm::vec3& pPrev,
                                                  const glm::vec3& pCur,
                                                  const glm::vec3& pNext,
                                                  const glm::vec3& nUnit,
                                                  float            amount) noexcept
    {
        glm::vec3 ePrev = project_to_plane(pCur - pPrev, nUnit);
        glm::vec3 eNext = project_to_plane(pNext - pCur, nUnit);

        ePrev = safe_normalize(ePrev, glm::vec3(1, 0, 0));
        eNext = safe_normalize(eNext, glm::vec3(1, 0, 0));

        // For CCW boundary with interior on the left and normal nUnit,
        // inward direction is approx cross(nUnit, edgeDir).
        const glm::vec3 inPrev = safe_normalize(glm::cross(nUnit, ePrev), glm::vec3(0));
        const glm::vec3 inNext = safe_normalize(glm::cross(nUnit, eNext), glm::vec3(0));

        glm::vec3 miter = inPrev + inNext;
        miter           = safe_normalize(miter, inPrev);

        float denom = glm::dot(miter, inPrev);
        if (std::abs(denom) < 1e-4f)
            denom = (denom < 0.0f) ? -1e-4f : 1e-4f;

        const float scale = amount / denom;
        return miter * scale;
    }

    // Store "a boundary corner attribute" we can copy to new loops.
    struct CornerAttrib
    {
        bool      hasN{false};
        bool      hasUV{false};
        glm::vec3 n{};
        glm::vec2 uv{};
    };

} // namespace

// ------------------------------------------------------------
// TRUE group inset (HeMesh)
// ------------------------------------------------------------
void InsetTool::insetPolys(SysMesh* mesh, std::span<const int32_t> polys, float amount, bool group)
{
    (void)group; // we *always* do the grouped behavior in this implementation.

    if (!mesh)
        return;

    if (glm::epsilonEqual(amount, 0.0f, 1e-9f))
        return;

    // ------------------------------------------------------------
    // 1) Extract the editable region to HeMesh (editable polys come from argument).
    // ------------------------------------------------------------
    HeExtractionOptions opt{};
    opt.includeBoundaryNeighbors = true; // keep a support ring for stable adjacency at boundary
    opt.importNormals            = true; // map ID 0 by your convention
    opt.importUVs                = true; // map ID 1 by your convention
    opt.normalMapId              = 0;
    opt.uvMapId                  = 1;

    HeExtractionResult extract = extract_polys_to_hemesh(mesh, polys, opt);
    HeMesh&            he      = extract.mesh;

    if (extract.editableSysPolys.empty())
        return;

    // Editable He poly set
    std::vector<HeMesh::PolyId> editableHePolys;
    editableHePolys.reserve(extract.editableSysPolys.size());

    std::unordered_set<int32_t> editableHeSet;
    editableHeSet.reserve(extract.editableSysPolys.size() * 2u);

    for (int32_t sp : extract.editableSysPolys)
    {
        if (sp < 0 || static_cast<size_t>(sp) >= extract.sysPolyToHePoly.size())
            continue;

        const int32_t hp = extract.sysPolyToHePoly[static_cast<size_t>(sp)];
        if (hp >= 0 && he.polyValid(hp))
        {
            editableHePolys.push_back(hp);
            editableHeSet.insert(hp);
        }
    }

    if (editableHePolys.empty())
        return;

    // ------------------------------------------------------------
    // 2) Identify boundary edges and build directed boundary half-edges.
    //
    // Boundary edge definition:
    //   an edge with exactly one incident editable polygon.
    //
    // Direction rule:
    //   for each boundary edge, we direct it according to the editable polygon winding:
    //   v_i -> v_{i+1} along that poly ring, so region interior stays on the left.
    // ------------------------------------------------------------
    struct DirBoundaryEdge
    {
        HeMesh::VertId a{HeMesh::kInvalidVert}; // start
        HeMesh::VertId b{HeMesh::kInvalidVert}; // end
        HeMesh::EdgeId e{HeMesh::kInvalidEdge};
        HeMesh::PolyId p{HeMesh::kInvalidPoly};
        int32_t        cornerA{-1}; // index in poly ring where verts[i]==a and verts[i+1]==b
    };

    std::vector<DirBoundaryEdge> directedBoundary;
    directedBoundary.reserve(editableHePolys.size() * 4u);

    // Also gather per-vertex corner attributes from the boundary (best effort).
    // We'll fill these from whatever boundary poly gave us the boundary direction.
    std::unordered_map<int32_t, CornerAttrib> boundaryAttrib; // key: outer vert id

    // Helper to count editable incident polys of an edge
    auto countEditableIncident = [&](HeMesh::EdgeId e) -> int32_t {
        int32_t    cnt = 0;
        const auto eps = he.edgePolys(e);
        for (int32_t i = 0; i < eps.size(); ++i)
        {
            const int32_t pid = eps[i];
            if (pid >= 0 && editableHeSet.contains(pid))
                ++cnt;
        }
        return cnt;
    };

    for (HeMesh::PolyId p : editableHePolys)
    {
        const auto pv = he.polyVerts(p);
        const auto pe = he.polyEdges(p);
        const auto pl = he.polyLoops(p);

        const int32_t n = pv.size();
        if (n < 3 || pe.size() != n || pl.size() != n)
            continue;

        for (int32_t i = 0; i < n; ++i)
        {
            const HeMesh::EdgeId e = pe[i];
            if (!he.edgeValid(e))
                continue;

            if (countEditableIncident(e) != 1)
                continue; // not a boundary edge of the editable region

            const HeMesh::VertId a = pv[i];
            const HeMesh::VertId b = pv[(i + 1) % n];

            DirBoundaryEdge de{};
            de.a       = a;
            de.b       = b;
            de.e       = e;
            de.p       = p;
            de.cornerA = i;
            directedBoundary.push_back(de);

            // Capture corner attribute for outer vertex a (loop at corner i).
            // Also capture for b from corner (i+1).
            if (opt.importNormals || opt.importUVs)
            {
                // a
                if (!boundaryAttrib.contains(a))
                {
                    CornerAttrib         ca{};
                    const HeMesh::LoopId la = pl[i];

                    if (opt.importNormals && he.loopHasNormal(la))
                    {
                        ca.hasN = true;
                        ca.n    = he.loopNormal(la);
                    }

                    if (opt.importUVs && he.loopHasUV(la))
                    {
                        ca.hasUV = true;
                        ca.uv    = he.loopUV(la);
                    }

                    boundaryAttrib.emplace(a, ca);
                }

                // b
                const int32_t j = (i + 1) % n;
                if (!boundaryAttrib.contains(b))
                {
                    CornerAttrib         cb{};
                    const HeMesh::LoopId lb = pl[j];

                    if (opt.importNormals && he.loopHasNormal(lb))
                    {
                        cb.hasN = true;
                        cb.n    = he.loopNormal(lb);
                    }

                    if (opt.importUVs && he.loopHasUV(lb))
                    {
                        cb.hasUV = true;
                        cb.uv    = he.loopUV(lb);
                    }

                    boundaryAttrib.emplace(b, cb);
                }
            }
        }
    }

    if (directedBoundary.empty())
        return; // no boundary => either nothing insettable or region is closed in a weird way

    // ------------------------------------------------------------
    // 3) Stitch directed boundary edges into one or more boundary loops.
    //
    // Expected manifold case:
    //   every boundary vertex has exactly one outgoing and one incoming edge per loop.
    // We'll still implement tracing defensively.
    // ------------------------------------------------------------
    // Build outgoing adjacency: start -> list of boundary edge indices
    std::unordered_map<int32_t, std::vector<int32_t>> outgoing;
    outgoing.reserve(directedBoundary.size());

    for (int32_t i = 0; i < static_cast<int32_t>(directedBoundary.size()); ++i)
    {
        outgoing[directedBoundary[static_cast<size_t>(i)].a].push_back(i);
    }

    std::vector<uint8_t> used(static_cast<size_t>(directedBoundary.size()), 0);

    struct BoundaryLoop
    {
        std::vector<HeMesh::VertId> verts; // ordered boundary verts
    };

    std::vector<BoundaryLoop> loops;
    loops.reserve(4);

    for (int32_t seed = 0; seed < static_cast<int32_t>(directedBoundary.size()); ++seed)
    {
        if (used[static_cast<size_t>(seed)] != 0)
            continue;

        // Start from this directed edge
        const DirBoundaryEdge& e0 = directedBoundary[static_cast<size_t>(seed)];

        BoundaryLoop loop{};
        loop.verts.reserve(64);

        HeMesh::VertId start = e0.a;
        // HeMesh::VertId cur   = e0.a;
        int32_t ei = seed;

        // Trace until we come back to start or we cannot continue
        for (int32_t guard = 0; guard < 1'000'000; ++guard)
        {
            if (ei < 0)
                break;

            if (used[static_cast<size_t>(ei)] != 0)
                break;

            used[static_cast<size_t>(ei)] = 1;

            const DirBoundaryEdge& de = directedBoundary[static_cast<size_t>(ei)];

            // Append vertex 'a' once; the next edge will append its 'a' which is this 'b'
            if (loop.verts.empty())
                loop.verts.push_back(de.a);

            const HeMesh::VertId next = de.b;

            if (next == start)
                break; // closed loop

            loop.verts.push_back(next);

            // Choose next unused outgoing edge from 'cur'
            int32_t nextEdge = -1;
            auto    it       = outgoing.find(next);
            if (it != outgoing.end())
            {
                for (int32_t cand : it->second)
                {
                    if (used[static_cast<size_t>(cand)] == 0)
                    {
                        nextEdge = cand;
                        break;
                    }
                }
            }

            ei = nextEdge;
        }

        if (loop.verts.size() >= 3)
            loops.push_back(std::move(loop));
    }

    if (loops.empty())
        return;

    // ------------------------------------------------------------
    // 4) Create inner boundary vertices (shared) and compute their positions.
    //
    // Important:
    //  - Only boundary vertices get duplicated.
    //  - These new verts will be referenced by the rebuilt editable polys.
    // ------------------------------------------------------------
    std::unordered_map<int32_t, int32_t> outerToInner;
    outerToInner.reserve(directedBoundary.size() * 2u);

    // Local normal per boundary vert = average normals of incident editable polys.
    auto boundaryVertNormal = [&](HeMesh::VertId v) -> glm::vec3 {
        glm::vec3  sum(0.0f);
        const auto vps = he.vertPolys(v);
        for (int32_t i = 0; i < vps.size(); ++i)
        {
            const int32_t pid = vps[i];
            if (pid >= 0 && editableHeSet.contains(pid))
                sum += he.polyNormal(pid);
        }
        return safe_normalize(sum, glm::vec3(0, 1, 0));
    };

    for (const BoundaryLoop& loop : loops)
    {
        const int32_t n = static_cast<int32_t>(loop.verts.size());
        if (n < 3)
            continue;

        for (int32_t i = 0; i < n; ++i)
        {
            const HeMesh::VertId vCur = loop.verts[static_cast<size_t>(i)];
            if (!he.vertValid(vCur))
                continue;

            if (outerToInner.contains(vCur))
                continue;

            const HeMesh::VertId vPrev = loop.verts[static_cast<size_t>((i - 1 + n) % n)];
            const HeMesh::VertId vNext = loop.verts[static_cast<size_t>((i + 1) % n)];

            const glm::vec3 pPrev = he.position(vPrev);
            const glm::vec3 pCur  = he.position(vCur);
            const glm::vec3 pNext = he.position(vNext);

            const glm::vec3 nUnit = boundaryVertNormal(vCur);

            const glm::vec3 delta   = compute_boundary_inset_delta(pPrev, pCur, pNext, nUnit, amount);
            const glm::vec3 innerPt = pCur + delta;

            const HeMesh::VertId vIn = he.createVert(innerPt);
            outerToInner.emplace(vCur, vIn);
        }
    }

    // If we failed to create any inner verts, nothing to do.
    if (outerToInner.empty())
        return;

    auto getInner = [&](HeMesh::VertId vOuter) -> HeMesh::VertId {
        auto it = outerToInner.find(vOuter);
        if (it != outerToInner.end())
            return it->second;
        return HeMesh::kInvalidVert;
    };

    // ------------------------------------------------------------
    // 5) Rebuild the editable polys by replacing boundary vertices with their inner copy.
    //
    // This is the key “true group inset” behavior:
    //  - internal vertices stay the same
    //  - boundary vertices are swapped to inner verts (shared across all faces)
    //  - internal topology (shared edges between editable polys) remains stitched
    // ------------------------------------------------------------
    struct NewPoly
    {
        uint32_t                    material{0};
        std::vector<HeMesh::VertId> verts;  // new ring
        std::vector<CornerAttrib>   corner; // per-corner attributes (best effort)
    };

    std::vector<NewPoly> rebuiltEditable;
    rebuiltEditable.reserve(editableHePolys.size());

    for (HeMesh::PolyId p : editableHePolys)
    {
        if (!he.polyValid(p))
            continue;

        const auto pv = he.polyVerts(p);
        const auto pl = he.polyLoops(p);

        const int32_t n = pv.size();
        if (n < 3 || pl.size() != n)
            continue;

        NewPoly np{};
        np.material = he.polyMaterial(p);
        np.verts.reserve(static_cast<size_t>(n));
        np.corner.reserve(static_cast<size_t>(n));

        const glm::vec3 fallbackN = he.polyNormal(p);

        for (int32_t i = 0; i < n; ++i)
        {
            const HeMesh::VertId vOuter = pv[i];
            HeMesh::VertId       vNew   = vOuter;

            // Replace only if it's a boundary vertex (has an inner copy).
            const HeMesh::VertId vIn = getInner(vOuter);
            if (vIn != HeMesh::kInvalidVert)
                vNew = vIn;

            np.verts.push_back(vNew);

            // Best-effort copy of per-corner attributes.
            CornerAttrib         ca{};
            const HeMesh::LoopId l = pl[i];

            if (opt.importNormals && he.loopHasNormal(l))
            {
                ca.hasN = true;
                ca.n    = he.loopNormal(l);
            }
            else
            {
                ca.hasN = true;
                ca.n    = fallbackN;
            }

            if (opt.importUVs && he.loopHasUV(l))
            {
                ca.hasUV = true;
                ca.uv    = he.loopUV(l);
            }
            else
            {
                ca.hasUV = false;
                ca.uv    = glm::vec2(0.0f);
            }

            np.corner.push_back(ca);
        }

        rebuiltEditable.push_back(std::move(np));
    }

    // ------------------------------------------------------------
    // 6) Remove original editable polys and create:
    //   (a) rebuilt editable polys (shrunk region)
    //   (b) rim quads along each boundary edge: [a, b, inner(b), inner(a)]
    //
    // Note:
    //  - We do NOT create a “cap poly”. This preserves holes correctly.
    //  - Rim only on boundary edges (exactly one editable incident poly).
    // ------------------------------------------------------------

    // Remove editable polys first (they will be replaced).
    for (HeMesh::PolyId p : editableHePolys)
        if (he.polyValid(p))
            he.removePoly(p);

    // Recreate the shrunk editable polys
    for (const NewPoly& np : rebuiltEditable)
    {
        if (np.verts.size() < 3)
            continue;

        const HeMesh::PolyId newP = he.createPoly(np.verts, np.material);

        // Restore per-corner attributes (best effort).
        const auto newLoops = he.polyLoops(newP);
        if (newLoops.size() == static_cast<int32_t>(np.corner.size()))
        {
            for (int32_t i = 0; i < newLoops.size(); ++i)
            {
                const CornerAttrib& ca = np.corner[static_cast<size_t>(i)];

                if (opt.importNormals && ca.hasN)
                    he.setLoopNormal(newLoops[i], ca.n);

                if (opt.importUVs && ca.hasUV)
                    he.setLoopUV(newLoops[i], ca.uv);
            }
        }
    }

    // Create rim quads. We use the directed boundary edges computed earlier.
    for (const DirBoundaryEdge& de : directedBoundary)
    {
        const HeMesh::VertId a  = de.a;
        const HeMesh::VertId b  = de.b;
        const HeMesh::VertId ia = getInner(a);
        const HeMesh::VertId ib = getInner(b);

        // If a/b didn't get inset (shouldn't happen on a proper boundary loop), skip.
        if (ia == HeMesh::kInvalidVert || ib == HeMesh::kInvalidVert)
            continue;

        // Rim quad: [a, b, ib, ia]
        std::vector<HeMesh::VertId> qv;
        qv.reserve(4);
        qv.push_back(a);
        qv.push_back(b);
        qv.push_back(ib);
        qv.push_back(ia);

        // Material: take from the boundary poly (the editable poly that owned this boundary edge).
        uint32_t mat = 0;
        if (he.polyValid(de.p))
            mat = he.polyMaterial(de.p);

        const HeMesh::PolyId qp = he.createPoly(qv, mat);

        // Best-effort per-corner attributes for the rim:
        //  - outer a uses boundary attrib[a] if present
        //  - outer b uses boundary attrib[b] if present
        //  - inner corners reuse the same attrib as their corresponding outer vertex
        const auto ql = he.polyLoops(qp);
        if (ql.size() == 4)
        {
            // fallback normal
            const glm::vec3 qN = he.polyNormal(qp);

            auto getAttrib = [&](HeMesh::VertId ov) -> CornerAttrib {
                auto it = boundaryAttrib.find(ov);
                if (it != boundaryAttrib.end())
                {
                    CornerAttrib out = it->second;
                    if (!out.hasN)
                    {
                        out.hasN = true;
                        out.n    = qN;
                    }
                    return out;
                }

                CornerAttrib out{};
                out.hasN  = true;
                out.n     = qN;
                out.hasUV = false;
                out.uv    = glm::vec2(0.0f);
                return out;
            };

            const CornerAttrib aa = getAttrib(a);
            const CornerAttrib bb = getAttrib(b);

            if (opt.importNormals && aa.hasN)
                he.setLoopNormal(ql[0], aa.n);
            if (opt.importUVs && aa.hasUV)
                he.setLoopUV(ql[0], aa.uv);

            if (opt.importNormals && bb.hasN)
                he.setLoopNormal(ql[1], bb.n);
            if (opt.importUVs && bb.hasUV)
                he.setLoopUV(ql[1], bb.uv);

            if (opt.importNormals && bb.hasN)
                he.setLoopNormal(ql[2], bb.n);
            if (opt.importUVs && bb.hasUV)
                he.setLoopUV(ql[2], bb.uv);

            if (opt.importNormals && aa.hasN)
                he.setLoopNormal(ql[3], aa.n);
            if (opt.importUVs && aa.hasUV)
                he.setLoopUV(ql[3], aa.uv);
        }
    }

    // Cleanup dangling topology after removals
    he.removeUnusedEdges();
    he.removeIsolatedVerts();

    // ------------------------------------------------------------
    // 7) Commit back to SysMesh (undo recorded by SysMesh APIs).
    // ------------------------------------------------------------
    const HeMeshCommit commit = build_commit_replace_editable(mesh, extract, he, opt);
    apply_commit(mesh, extract, commit, opt);
}
