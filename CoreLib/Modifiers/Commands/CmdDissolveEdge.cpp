#include "CmdDissolveEdge.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HeMeshBridge.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SelectionUtils.hpp"
#include "SysMesh.hpp"
#include "SysMeshUtils.hpp"

/// Use SysMesh
// bool CmdDissolveEdge::execute(Scene* scene)
// {
//     for (SysMesh* mesh : scene->activeMeshes())
//     {
//         std::vector<OrderedEdgePath> orderedEdgePaths = build_ordered_edge_paths(mesh->selected_edges());

//         for (const OrderedEdgePath& oep : orderedEdgePaths)
//         {
//             IndexPair edge = oep.edges.back(); // try for one edge of a cube only

//             SysVertPolys vp0 = mesh->vert_polys(edge.first);
//             SysVertPolys vp1 = mesh->vert_polys(edge.second);

//             std::unordered_set<int32_t> procecedPolys;

//             for (int32_t poly : vp0)
//             {
//                 if (!mesh->poly_has_edge(poly, edge)) // its one of the side polys
//                 {
//                     SysPolyVerts pv = mesh->poly_verts(poly);
//                     if (pv.contains(edge.first))
//                     {
//                         pv.erase_element(edge.first);
//                     }
//                     else if (pv.contains(edge.second))
//                     {
//                         pv.erase_element(edge.second);
//                     }
//                     [[maybe_unused]] int32_t sidePoly0 = mesh->clone_poly(poly, pv);

//                     procecedPolys.insert(poly);
//                     // mesh->remove_poly(poly);
//                 }
//             }

//             for (int32_t poly : vp1)
//             {
//                 if (!mesh->poly_has_edge(poly, edge)) // its one of the side polys
//                 {
//                     SysPolyVerts pv = mesh->poly_verts(poly);
//                     if (pv.contains(edge.first))
//                     {
//                         pv.erase_element(edge.first);
//                     }
//                     else if (pv.contains(edge.second))
//                     {
//                         pv.erase_element(edge.second);
//                     }
//                     [[maybe_unused]] int32_t sidePoly0 = mesh->clone_poly(poly, pv);
//                     // mesh->remove_poly(poly);
//                     procecedPolys.insert(poly);
//                 }
//             }

//             // ------------------------------------------------------------
//             // NEW: replace the two edge polys with ONE quad
//             // ------------------------------------------------------------
//             SysEdgePolys ep = mesh->edge_polys(edge);
//             if (ep.size() == 2 && mesh->poly_valid(ep[0]) && mesh->poly_valid(ep[1]))
//             {
//                 int32_t p0 = ep[0];
//                 int32_t p1 = ep[1];

//                 SysPolyVerts pv0 = mesh->poly_verts(p0);
//                 SysPolyVerts pv1 = mesh->poly_verts(p1);

//                 // remove the shared edge endpoints from each face
//                 pv0.erase_element(edge.first);
//                 pv0.erase_element(edge.second);

//                 pv1.erase_element(edge.first);
//                 pv1.erase_element(edge.second);

//                 // cube case: each face becomes 2 verts, making a quad
//                 if (pv0.size() == 2 && pv1.size() == 2)
//                 {
//                     SysPolyVerts quadA;
//                     quadA.push_back(pv0[0]);
//                     quadA.push_back(pv0[1]);
//                     quadA.push_back(pv1[0]);
//                     quadA.push_back(pv1[1]);

//                     SysPolyVerts quadB;
//                     quadB.push_back(pv0[0]);
//                     quadB.push_back(pv0[1]);
//                     quadB.push_back(pv1[1]);
//                     quadB.push_back(pv1[0]);

//                     // pick the winding that best matches existing boundary edges
//                     auto score = [&](const SysPolyVerts& q) -> int {
//                         int s = 0;
//                         if (!mesh->edge_polys(SysMesh::sort_edge({q[0], q[1]})).empty())
//                             ++s;
//                         if (!mesh->edge_polys(SysMesh::sort_edge({q[1], q[2]})).empty())
//                             ++s;
//                         if (!mesh->edge_polys(SysMesh::sort_edge({q[2], q[3]})).empty())
//                             ++s;
//                         if (!mesh->edge_polys(SysMesh::sort_edge({q[3], q[0]})).empty())
//                             ++s;
//                         return s;
//                     };

//                     const int sA = score(quadA);
//                     const int sB = score(quadB);

//                     const uint32_t mat = mesh->poly_material(p0);

//                     mesh->create_poly((sB > sA) ? quadB : quadA, mat);

//                     // remove the original two faces that contained the dissolved edge
//                     procecedPolys.insert(p0);
//                     procecedPolys.insert(p1);
//                 }
//             }

//             // ------------------------------------------------------------
//             // Remove originals at end (your approach)
//             // ------------------------------------------------------------
//             for (int32_t poly : procecedPolys)
//             {
//                 if (mesh->poly_valid(poly))
//                     mesh->remove_poly(poly);
//             }
//         }
//     }

//     return true;
// }

/// Use HeMesh (one cube edge only)
// bool CmdDissolveEdge::execute(Scene* scene)
// {
//     if (!scene)
//         return false;

//     for (SysMesh* mesh : scene->activeMeshes())
//     {
//         if (!mesh)
//             continue;

//         std::vector<OrderedEdgePath> orderedEdgePaths =
//             build_ordered_edge_paths(mesh->selected_edges());

//         for (const OrderedEdgePath& oep : orderedEdgePaths)
//         {
//             if (oep.edges.empty())
//                 continue;

//             // ---- cube test: do ONE edge only (exactly like we did) ----
//             IndexPair edge = oep.edges.back();

//             if (!mesh->vert_valid(edge.first) || !mesh->vert_valid(edge.second))
//                 continue;

//             // ------------------------------------------------------------
//             // Build editable region = edge polys + side polys around endpoints
//             // (matches your working SysMesh experiment)
//             // ------------------------------------------------------------
//             std::unordered_set<int32_t> editableSet;
//             editableSet.reserve(16);

//             SysEdgePolys ep = mesh->edge_polys(edge);
//             if (ep.size() != 2)
//                 continue; // cube test expects 2

//             for (int32_t p : ep)
//                 if (mesh->poly_valid(p))
//                     editableSet.insert(p);

//             SysVertPolys vp0 = mesh->vert_polys(edge.first);
//             for (int32_t p : vp0)
//             {
//                 if (!mesh->poly_valid(p))
//                     continue;
//                 if (mesh->poly_has_edge(p, edge))
//                     continue;
//                 editableSet.insert(p);
//             }

//             SysVertPolys vp1 = mesh->vert_polys(edge.second);
//             for (int32_t p : vp1)
//             {
//                 if (!mesh->poly_valid(p))
//                     continue;
//                 if (mesh->poly_has_edge(p, edge))
//                     continue;
//                 editableSet.insert(p);
//             }

//             std::vector<int32_t> editableSysPolys;
//             editableSysPolys.reserve(editableSet.size());
//             for (int32_t p : editableSet)
//                 editableSysPolys.push_back(p);

//             // ------------------------------------------------------------
//             // Extract into HeMesh (no neighbor ring for this minimal test)
//             // ------------------------------------------------------------
//             HeExtractionOptions opt{};
//             opt.includeBoundaryNeighbors = false;
//             opt.importNormals            = false;
//             opt.importUVs                = false;

//             HeExtractionResult extract =
//                 extract_polys_to_hemesh(mesh, editableSysPolys, opt);

//             if (extract.editableSysPolys.empty())
//                 continue;

//             HeMesh& he = extract.mesh;

//             const int32_t ha = extract.sysVertToHeVert[edge.first];
//             const int32_t hb = extract.sysVertToHeVert[edge.second];
//             if (ha < 0 || hb < 0)
//                 continue;

//             if (!he.vertValid(ha) || !he.vertValid(hb))
//                 continue;

//             HeMesh::EdgeId heEdge = he.findEdge(ha, hb);
//             if (!he.edgeValid(heEdge))
//                 continue;

//             // ------------------------------------------------------------
//             // A) “side polys” -> triangles (remove ha OR hb)
//             // ------------------------------------------------------------

//             for (HeMesh::PolyId p : he.allPolys())
//             {
//                 if (!he.polyValid(p))
//                     continue;

//                 // skip the 2 edge polys (we’ll replace them with a quad)
//                 bool isEdgePoly = false;
//                 for (HeMesh::EdgeId pe : he.polyEdges(p))
//                 {
//                     if (pe == heEdge)
//                     {
//                         isEdgePoly = true;
//                         break;
//                     }
//                 }
//                 if (isEdgePoly)
//                     continue;

//                 auto pv = he.polyVerts(p);

//                 if (pv.contains(ha))
//                 {
//                     pv.erase_element(ha);
//                     if (pv.size() >= 3)
//                         he.setPolyVerts(p, {pv.begin(), pv.end()});
//                 }
//                 else if (pv.contains(hb))
//                 {
//                     pv.erase_element(hb);
//                     if (pv.size() >= 3)
//                         he.setPolyVerts(p, {pv.begin(), pv.end()});
//                 }
//             }

//             // ------------------------------------------------------------
//             // B) Replace the 2 edge polys with ONE quad from the 4 “other” corners
//             // ------------------------------------------------------------
//             HeMesh::EdgePolys heEdgePolys = he.edgePolys(heEdge);
//             if (heEdgePolys.size() != 2)
//                 continue;

//             HeMesh::PolyId p0 = heEdgePolys[0];
//             HeMesh::PolyId p1 = heEdgePolys[1];

//             if (!he.polyValid(p0) || !he.polyValid(p1))
//                 continue;

//             const uint32_t mat = he.polyMaterial(p0);

//             auto pick_opposites = [&](HeMesh::PolyId  p,
//                                       HeMesh::VertId  a,
//                                       HeMesh::VertId  b,
//                                       HeMesh::VertId& outAdjA,
//                                       HeMesh::VertId& outAdjB) -> bool {
//                 auto      pv = he.polyVerts(p);
//                 const int n  = (int)pv.size();
//                 if (n < 4)
//                     return false;

//                 int ia = -1, ib = -1;
//                 for (int i = 0; i < n; ++i)
//                 {
//                     if (pv[i] == a)
//                         ia = i;
//                     if (pv[i] == b)
//                         ib = i;
//                 }
//                 if (ia < 0 || ib < 0)
//                     return false;

//                 const bool ab_fwd = (pv[(ia + 1) % n] == b);
//                 const bool ab_bwd = (pv[(ib + 1) % n] == a);
//                 if (!ab_fwd && !ab_bwd)
//                     return false;

//                 if (ab_fwd)
//                 {
//                     outAdjA = pv[(ia + n - 1) % n];
//                     outAdjB = pv[(ib + 1) % n];
//                 }
//                 else
//                 {
//                     outAdjA = pv[(ia + 1) % n];
//                     outAdjB = pv[(ib + n - 1) % n];
//                 }
//                 return true;
//             };

//             HeMesh::VertId a0 = HeMesh::kInvalidVert, b0 = HeMesh::kInvalidVert;
//             HeMesh::VertId a1 = HeMesh::kInvalidVert, b1 = HeMesh::kInvalidVert;

//             if (!pick_opposites(p0, ha, hb, a0, b0))
//                 if (!pick_opposites(p0, hb, ha, b0, a0))
//                     continue;

//             if (!pick_opposites(p1, ha, hb, a1, b1))
//                 if (!pick_opposites(p1, hb, ha, b1, a1))
//                     continue;

//             if (!he.vertValid(a0) || !he.vertValid(b0) || !he.vertValid(a1) || !he.vertValid(b1))
//                 continue;

//             he.removePoly(p0);
//             he.removePoly(p1);

//             // cube-case quad order (may flip winding depending on face orientation)
//             std::vector<HeMesh::VertId> quad = {a0, b0, b1, a1};
//             he.createPoly(quad, mat);

//             he.removeUnusedEdges();
//             he.removeIsolatedVerts();

//             // ------------------------------------------------------------
//             // Commit back (replace only editable polys)
//             // ------------------------------------------------------------
//             const HeMeshCommit commit =
//                 build_commit_replace_editable(mesh, extract, he, opt);

//             apply_commit(mesh, extract, commit, opt);
//         }
//     }

//     return true;
// }

bool CmdDissolveEdge::execute(Scene* scene)
{
    if (!scene)
        return false;

    for (SysMesh* mesh : scene->activeMeshes())
    {
        if (!mesh)
            continue;

        std::vector<smu::OrderedEdgePath> orderedEdgePaths = smu::build_ordered_edge_paths(mesh->selected_edges());

        for (const smu::OrderedEdgePath& oep : orderedEdgePaths)
        {
            if (oep.edges.empty())
                continue;

            // ------------------------------------------------------------
            // Build fast lookup sets for this path
            // ------------------------------------------------------------
            std::unordered_set<int32_t> stripVerts;
            stripVerts.reserve(oep.verts.size() * 2);

            for (int32_t v : oep.verts)
                if (mesh->vert_valid(v))
                    stripVerts.insert(v);

            // Selected edges are already canonical in OrderedEdgePath by your comment.
            // Still, we keep them canonical for safety if anything changes later.
            std::unordered_set<IndexPair, smu::IndexPairHash> stripEdges;
            stripEdges.reserve(oep.edges.size() * 2);

            for (const IndexPair& e : oep.edges)
            {
                if (!mesh->vert_valid(e.first) || !mesh->vert_valid(e.second))
                    continue;

                stripEdges.insert(SysMesh::sort_edge(e));
            }

            if (stripEdges.empty())
                continue;

            auto poly_has_any_strip_edge = [&](int32_t poly) -> bool {
                if (!mesh->poly_valid(poly))
                    return false;

                // Check only edges that touch strip verts (cheap filter):
                // (Still simple: scan stripEdges; small in tools.)
                for (const IndexPair& e : stripEdges)
                {
                    if (mesh->poly_has_edge(poly, e))
                        return true;
                }
                return false;
            };

            std::unordered_set<int32_t> processedPolys;
            processedPolys.reserve(oep.edges.size() * 8);

            // ------------------------------------------------------------
            // A) Side polys: any poly touching strip verts but NOT a strip poly
            // Remove ALL strip verts contained by that poly, clone, mark original for removal.
            // ------------------------------------------------------------
            std::unordered_set<int32_t> sidePolys;
            sidePolys.reserve(oep.verts.size() * 8);

            for (int32_t v : oep.verts)
            {
                if (!mesh->vert_valid(v))
                    continue;

                SysVertPolys vp = mesh->vert_polys(v);
                for (int32_t poly : vp)
                {
                    if (!mesh->poly_valid(poly))
                        continue;

                    if (poly_has_any_strip_edge(poly))
                        continue; // strip poly, handled below

                    sidePolys.insert(poly);
                }
            }

            for (int32_t poly : sidePolys)
            {
                if (!mesh->poly_valid(poly))
                    continue;

                SysPolyVerts pv = mesh->poly_verts(poly);

                bool changed = false;
                for (int32_t sv : stripVerts)
                {
                    if (pv.contains(sv))
                    {
                        pv.erase_element(sv);
                        changed = true;
                    }
                }

                if (!changed)
                    continue;

                if (pv.size() >= 3)
                {
                    [[maybe_unused]] int32_t sidePolyNew = mesh->clone_poly(poly, pv);
                    processedPolys.insert(poly);
                }
            }

            // ------------------------------------------------------------
            // B) For EACH selected edge: replace its two edge polys with ONE quad
            // ------------------------------------------------------------
            auto score = [&](const SysPolyVerts& q) -> int {
                int s = 0;

                if (!mesh->edge_polys(SysMesh::sort_edge({q[0], q[1]})).empty())
                    ++s;
                if (!mesh->edge_polys(SysMesh::sort_edge({q[1], q[2]})).empty())
                    ++s;
                if (!mesh->edge_polys(SysMesh::sort_edge({q[2], q[3]})).empty())
                    ++s;
                if (!mesh->edge_polys(SysMesh::sort_edge({q[3], q[0]})).empty())
                    ++s;

                return s;
            };

            for (const IndexPair& eIn : oep.edges)
            {
                const IndexPair edge = SysMesh::sort_edge(eIn);

                if (!mesh->vert_valid(edge.first) || !mesh->vert_valid(edge.second))
                    continue;

                SysEdgePolys ep = mesh->edge_polys(edge);
                if (ep.size() != 2)
                    continue;

                const int32_t p0 = ep[0];
                const int32_t p1 = ep[1];

                if (!mesh->poly_valid(p0) || !mesh->poly_valid(p1))
                    continue;

                SysPolyVerts pv0 = mesh->poly_verts(p0);
                SysPolyVerts pv1 = mesh->poly_verts(p1);

                // remove the shared edge endpoints from each face
                pv0.erase_element(edge.first);
                pv0.erase_element(edge.second);

                pv1.erase_element(edge.first);
                pv1.erase_element(edge.second);

                // For the segmented-cube strip case: both should become 2 verts
                if (pv0.size() != 2 || pv1.size() != 2)
                    continue;

                SysPolyVerts quadA;
                quadA.push_back(pv0[0]);
                quadA.push_back(pv0[1]);
                quadA.push_back(pv1[0]);
                quadA.push_back(pv1[1]);

                SysPolyVerts quadB;
                quadB.push_back(pv0[0]);
                quadB.push_back(pv0[1]);
                quadB.push_back(pv1[1]);
                quadB.push_back(pv1[0]);

                const int sA = score(quadA);
                const int sB = score(quadB);

                const uint32_t mat = mesh->poly_material(p0);

                // Create replacement quad
                mesh->create_poly((sB > sA) ? quadB : quadA, mat);

                // Remove originals later
                processedPolys.insert(p0);
                processedPolys.insert(p1);
            }

            // ------------------------------------------------------------
            // Remove originals at end (your approach)
            // ------------------------------------------------------------
            for (int32_t poly : processedPolys)
            {
                if (mesh->poly_valid(poly))
                    mesh->remove_poly(poly);
            }

            // ------------------------------------------------------------
            // NEW: remove stray strip verts (no full-mesh scan)
            // ------------------------------------------------------------
            std::vector<int32_t> deadVerts;
            deadVerts.reserve(stripVerts.size());

            for (int32_t v : stripVerts)
            {
                if (!mesh->vert_valid(v))
                    continue;

                bool         used = false;
                SysVertPolys vp   = mesh->vert_polys(v);

                for (int32_t p : vp)
                {
                    if (mesh->poly_valid(p))
                    {
                        used = true;
                        break;
                    }
                }

                if (!used)
                    deadVerts.push_back(v);
            }

            // Remove after collection (safer than removing while iterating adjacency)
            for (int32_t v : deadVerts)
            {
                if (mesh->vert_valid(v))
                    mesh->remove_vert(v);
            }
        }
    }

    return true;
}
