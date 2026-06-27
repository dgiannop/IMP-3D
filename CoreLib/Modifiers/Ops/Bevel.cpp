// #include "Ops/Bevel.hpp"

// #include <algorithm>
// #include <cstdint>
// #include <glm/glm.hpp>
// #include <iostream>
// #include <unordered_map>
// #include <unordered_set>
// #include <utility>
// #include <vector>

// #include "CoreUtilities.hpp"
// #include "HeMeshBridge.hpp"
// #include "SysMesh.hpp"

// namespace ops::sys
// {

// } // namespace ops::sys

// namespace ops::sys
// {
//     void bevelEdges(SysMesh* mesh, std::span<const IndexPair> edges, float width)
//     {
//         if (!mesh || edges.empty())
//             return;

//         float w = width;
//         if (w < 0.0f)
//             w = -w;

//         if (un::is_zero(w))
//             return;

//         // ------------------------------------------------------------
//         // Helpers (bevel-local only)
//         // ------------------------------------------------------------

//         // For CCW poly ring: cross(N, d) points inward (left of directed edge)
//         auto inward_dir = [&](int32_t p, int32_t v0, int32_t v1) noexcept -> glm::vec3 {
//             const glm::vec3 N = mesh->poly_normal(p);
//             if (glm::dot(N, N) < 1e-12f)
//                 return glm::vec3(0.0f);

//             const glm::vec3& P0 = mesh->vert_position(v0);
//             const glm::vec3& P1 = mesh->vert_position(v1);

//             glm::vec3 d  = un::safe_normalize(P1 - P0);
//             glm::vec3 in = glm::cross(N, d);
//             return un::safe_normalize(in);
//         };

//         // ------------------------------------------------------------
//         // 1) Normalize + unique selected edges (undirected)
//         // ------------------------------------------------------------
//         std::vector<IndexPair> selEdges;
//         selEdges.reserve(edges.size());

//         std::unordered_set<uint64_t> selEdgeSet;
//         selEdgeSet.reserve(edges.size() * 2u);

//         for (const IndexPair& e0 : edges)
//         {
//             if (e0.first < 0 || e0.second < 0)
//                 continue;
//             if (e0.first == e0.second)
//                 continue;
//             if (!mesh->vert_valid(e0.first) || !mesh->vert_valid(e0.second))
//                 continue;

//             const IndexPair e = mesh->sort_edge(e0);
//             const uint64_t  k = un::pack_undirected_i32(e.first, e.second);

//             if (!selEdgeSet.insert(k).second)
//                 continue;

//             selEdges.push_back(e);
//         }

//         if (selEdges.empty())
//             return;

//         auto is_sel_edge = [&](int32_t a, int32_t b) noexcept -> bool {
//             return selEdgeSet.contains(un::pack_undirected_i32(a, b));
//         };

//         // ------------------------------------------------------------
//         // 2) Editable polys = all polys incident to selected edges
//         // ------------------------------------------------------------
//         std::vector<int32_t> editablePolys;
//         editablePolys.reserve(selEdges.size() * 2u);

//         std::unordered_set<int32_t> editableSet;
//         editableSet.reserve(selEdges.size() * 4u);

//         for (const IndexPair& e : selEdges)
//         {
//             const SysEdgePolys adj = mesh->edge_polys(e);
//             for (int32_t p : adj)
//             {
//                 if (p < 0 || !mesh->poly_valid(p))
//                     continue;

//                 if (editableSet.insert(p).second)
//                     editablePolys.push_back(p);
//             }
//         }

//         if (editablePolys.empty())
//             return;

//         // ------------------------------------------------------------
//         // 3) Cache selected-edge adjacency BEFORE edits
//         // ------------------------------------------------------------
//         struct EdgeInfo
//         {
//             IndexPair            e{};
//             std::vector<int32_t> polys{};
//         };

//         std::vector<EdgeInfo> edgeInfos;
//         edgeInfos.reserve(selEdges.size());

//         for (const IndexPair& e : selEdges)
//         {
//             EdgeInfo info{};
//             info.e = e;

//             const SysEdgePolys adj = mesh->edge_polys(e);
//             for (int32_t p : adj)
//             {
//                 if (p >= 0 && mesh->poly_valid(p))
//                     info.polys.push_back(p);
//             }

//             edgeInfos.push_back(std::move(info));
//         }

//         // ------------------------------------------------------------
//         // 4) Poly groups: flood fill within editable across NON-selected edges
//         // ------------------------------------------------------------
//         std::unordered_map<int32_t, int32_t> polyGroup;
//         polyGroup.reserve(editablePolys.size() * 2u);

//         int32_t nextGroup = 0;

//         for (int32_t seed : editablePolys)
//         {
//             if (!mesh->poly_valid(seed))
//                 continue;

//             if (polyGroup.contains(seed))
//                 continue;

//             const int32_t gid = nextGroup++;
//             polyGroup.emplace(seed, gid);

//             std::vector<int32_t> stack;
//             stack.push_back(seed);

//             while (!stack.empty())
//             {
//                 const int32_t p = stack.back();
//                 stack.pop_back();

//                 if (!mesh->poly_valid(p))
//                     continue;

//                 const SysPolyVerts& pv = mesh->poly_verts(p);
//                 if (pv.size() < 3)
//                     continue;

//                 for (int i = 0; i < pv.size(); ++i)
//                 {
//                     const int32_t a = pv[i];
//                     const int32_t b = pv[(i + 1) % pv.size()];

//                     if (a < 0 || b < 0)
//                         continue;

//                     if (is_sel_edge(a, b))
//                         continue; // selected edges separate groups

//                     const IndexPair    ue  = mesh->sort_edge({a, b});
//                     const SysEdgePolys adj = mesh->edge_polys(ue);

//                     for (int32_t q : adj)
//                     {
//                         if (q == p)
//                             continue;
//                         if (!editableSet.contains(q))
//                             continue;
//                         if (polyGroup.contains(q))
//                             continue;

//                         polyGroup.emplace(q, gid);
//                         stack.push_back(q);
//                     }
//                 }
//             }
//         }

//         // ------------------------------------------------------------
//         // 5) Shared inset verts per (groupId, originalVert)
//         // ------------------------------------------------------------
//         struct InsetKey
//         {
//             int32_t g{};
//             int32_t v{};

//             bool operator==(const InsetKey& o) const noexcept
//             {
//                 return g == o.g && v == o.v;
//             }
//         };

//         struct InsetKeyHash
//         {
//             std::size_t operator()(const InsetKey& k) const noexcept
//             {
//                 std::size_t h1 = std::hash<int32_t>{}(k.g);
//                 std::size_t h2 = std::hash<int32_t>{}(k.v);
//                 return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2));
//             }
//         };

//         struct Accum
//         {
//             glm::vec3 sum{0.0f};
//             int32_t   count{0};
//         };

//         std::unordered_map<InsetKey, Accum, InsetKeyHash> insetAccum;
//         insetAccum.reserve(4096);

//         auto add_inset_sample = [&](int32_t gid, int32_t v, const glm::vec3& pos) {
//             InsetKey k{gid, v};
//             auto&    a = insetAccum[k];
//             a.sum += pos;
//             a.count += 1;
//         };

//         for (int32_t p : editablePolys)
//         {
//             if (!mesh->poly_valid(p))
//                 continue;

//             auto itg = polyGroup.find(p);
//             if (itg == polyGroup.end())
//                 continue;

//             const int32_t gid = itg->second;

//             const SysPolyVerts& pv = mesh->poly_verts(p);
//             const int           n  = pv.size();
//             if (n < 3)
//                 continue;

//             const glm::vec3 N = mesh->poly_normal(p);
//             if (glm::dot(N, N) < 1e-12f)
//                 continue;

//             glm::vec3 U(0.0f), V(0.0f);
//             un::make_basis(N, U, V);

//             for (int i = 0; i < n; ++i)
//             {
//                 const int32_t vPrev = pv[(i + n - 1) % n];
//                 const int32_t v     = pv[i];
//                 const int32_t vNext = pv[(i + 1) % n];

//                 if (!mesh->vert_valid(vPrev) || !mesh->vert_valid(v) || !mesh->vert_valid(vNext))
//                     continue;

//                 const bool selIn  = is_sel_edge(vPrev, v);
//                 const bool selOut = is_sel_edge(v, vNext);

//                 if (!selIn && !selOut)
//                     continue;

//                 const glm::vec3 P = mesh->vert_position(v);

//                 struct Line2
//                 {
//                     glm::vec2 p{}, d{};
//                     bool      valid{false};
//                 };
//                 Line2 L0{}, L1{};

//                 if (selIn)
//                 {
//                     const glm::vec3 in  = inward_dir(p, vPrev, v);
//                     const glm::vec3 p0w = P + in * w;
//                     const glm::vec3 d0w = un::safe_normalize(P - mesh->vert_position(vPrev));

//                     L0.p     = glm::vec2(glm::dot(p0w, U), glm::dot(p0w, V));
//                     L0.d     = glm::vec2(glm::dot(d0w, U), glm::dot(d0w, V));
//                     L0.valid = true;
//                 }

//                 if (selOut)
//                 {
//                     const glm::vec3 in  = inward_dir(p, v, vNext);
//                     const glm::vec3 p1w = P + in * w;
//                     const glm::vec3 d1w = un::safe_normalize(mesh->vert_position(vNext) - P);

//                     if (!L0.valid)
//                     {
//                         L0.p     = glm::vec2(glm::dot(p1w, U), glm::dot(p1w, V));
//                         L0.d     = glm::vec2(glm::dot(d1w, U), glm::dot(d1w, V));
//                         L0.valid = true;
//                     }
//                     else
//                     {
//                         L1.p     = glm::vec2(glm::dot(p1w, U), glm::dot(p1w, V));
//                         L1.d     = glm::vec2(glm::dot(d1w, U), glm::dot(d1w, V));
//                         L1.valid = true;
//                     }
//                 }

//                 glm::vec3 newPos = P;

//                 if (L0.valid && L1.valid)
//                 {
//                     glm::vec2 isect{};
//                     if (un::intersect_lines_2d(L0.p, L0.d, L1.p, L1.d, isect))
//                     {
//                         const float h = glm::dot(P, N);
//                         newPos        = isect.x * U + isect.y * V + h * N;
//                     }
//                     else
//                     {
//                         glm::vec3 inSum(0.0f);
//                         if (selIn)
//                             inSum += inward_dir(p, vPrev, v);
//                         if (selOut)
//                             inSum += inward_dir(p, v, vNext);

//                         inSum = un::safe_normalize(inSum);
//                         if (glm::dot(inSum, inSum) > 0.0f)
//                             newPos = P + inSum * w;
//                     }
//                 }
//                 else if (L0.valid)
//                 {
//                     glm::vec3 inSum(0.0f);
//                     if (selIn)
//                         inSum += inward_dir(p, vPrev, v);
//                     if (selOut)
//                         inSum += inward_dir(p, v, vNext);

//                     inSum = un::safe_normalize(inSum);
//                     if (glm::dot(inSum, inSum) > 0.0f)
//                         newPos = P + inSum * w;
//                 }

//                 add_inset_sample(gid, v, newPos);
//             }
//         }

//         std::unordered_map<InsetKey, int32_t, InsetKeyHash> insetVert;
//         insetVert.reserve(insetAccum.size() * 2u);

//         auto inset_for = [&](int32_t gid, int32_t v) noexcept -> int32_t {
//             const InsetKey k{gid, v};
//             auto           it = insetVert.find(k);
//             return (it != insetVert.end()) ? it->second : -1;
//         };

//         for (const auto& [k, a] : insetAccum)
//         {
//             if (a.count <= 0)
//                 continue;

//             const glm::vec3 pos = a.sum / float(a.count);
//             const int32_t   nv  = mesh->create_vert(pos);
//             insetVert.emplace(k, nv);
//         }

//         if (insetVert.empty())
//             return;

//         // ------------------------------------------------------------
//         // 6) Build replacement polys for editable region
//         // ------------------------------------------------------------
//         struct NewPoly
//         {
//             SysPolyVerts verts{};
//             uint32_t     material{0};
//         };

//         std::vector<NewPoly> newEditablePolys;
//         newEditablePolys.reserve(editablePolys.size());

//         for (int32_t p : editablePolys)
//         {
//             if (!mesh->poly_valid(p))
//                 continue;

//             auto itg = polyGroup.find(p);
//             if (itg == polyGroup.end())
//                 continue;

//             const int32_t gid = itg->second;

//             const SysPolyVerts& pv = mesh->poly_verts(p);
//             const int           n  = pv.size();
//             if (n < 3)
//                 continue;

//             SysPolyVerts out{};
//             out.reserve(n);

//             for (int i = 0; i < n; ++i)
//             {
//                 const int32_t vPrev = pv[(i + n - 1) % n];
//                 const int32_t v     = pv[i];
//                 const int32_t vNext = pv[(i + 1) % n];

//                 const bool touches = is_sel_edge(vPrev, v) || is_sel_edge(v, vNext);

//                 if (touches)
//                 {
//                     const int32_t vi = inset_for(gid, v);
//                     out.push_back((vi >= 0) ? vi : v);
//                 }
//                 else
//                 {
//                     out.push_back(v);
//                 }
//             }

//             if (out.size() >= 3)
//             {
//                 NewPoly np{};
//                 np.verts    = out;
//                 np.material = mesh->poly_material(p);
//                 newEditablePolys.push_back(std::move(np));
//             }
//         }

//         // ------------------------------------------------------------
//         // 7) Band quads across each manifold selected edge
//         // ------------------------------------------------------------
//         std::vector<NewPoly> bandQuads;
//         bandQuads.reserve(selEdges.size());

//         std::unordered_set<uint64_t> bridged;
//         bridged.reserve(selEdges.size() * 2u);

//         for (const EdgeInfo& info : edgeInfos)
//         {
//             int32_t p = -1;
//             int32_t q = -1;

//             for (int32_t pid : info.polys)
//             {
//                 if (!editableSet.contains(pid))
//                     continue;

//                 if (p < 0)
//                     p = pid;
//                 else
//                 {
//                     q = pid;
//                     break;
//                 }
//             }

//             if (p < 0 || q < 0)
//                 continue;

//             const int32_t gp = polyGroup.contains(p) ? polyGroup[p] : -1;
//             const int32_t gq = polyGroup.contains(q) ? polyGroup[q] : -1;
//             if (gp < 0 || gq < 0)
//                 continue;

//             const int32_t a = info.e.first;
//             const int32_t b = info.e.second;

//             const uint64_t ek = un::pack_undirected_i32(a, b);
//             if (bridged.contains(ek))
//                 continue;

//             const int32_t a2p = inset_for(gp, a);
//             const int32_t b2p = inset_for(gp, b);
//             const int32_t a2q = inset_for(gq, a);
//             const int32_t b2q = inset_for(gq, b);

//             if (a2p < 0 || b2p < 0 || a2q < 0 || b2q < 0)
//                 continue;

//             SysPolyVerts quad{};
//             quad.reserve(4);
//             quad.push_back(a2p);
//             quad.push_back(b2p);
//             quad.push_back(b2q);
//             quad.push_back(a2q);

//             {
//                 glm::vec3        N  = un::safe_normalize(mesh->poly_normal(p) + mesh->poly_normal(q));
//                 const glm::vec3& P0 = mesh->vert_position(quad[0]);
//                 const glm::vec3& P1 = mesh->vert_position(quad[1]);
//                 const glm::vec3& P2 = mesh->vert_position(quad[2]);
//                 glm::vec3        Nb = un::safe_normalize(glm::cross(P1 - P0, P2 - P0));
//                 if (glm::dot(Nb, N) < 0.0f)
//                     std::swap(quad[1], quad[3]);
//             }

//             NewPoly band{};
//             band.material = mesh->poly_material(p);
//             band.verts    = quad;
//             bandQuads.push_back(std::move(band));

//             bridged.insert(ek);
//         }

//         // ------------------------------------------------------------
//         // 8) Apply: remove editable polys, then create new ones
//         // ------------------------------------------------------------
//         for (int32_t p : editablePolys)
//             if (mesh->poly_valid(p))
//                 mesh->remove_poly(p);

//         for (const NewPoly& np : newEditablePolys)
//             if (np.verts.size() >= 3)
//                 mesh->create_poly(np.verts, np.material);

//         for (const NewPoly& np : bandQuads)
//             if (np.verts.size() >= 3)
//                 mesh->create_poly(np.verts, np.material);
//     }

//     void bevelPolys(SysMesh* mesh, std::span<const int32_t> polys, float amount, bool group)
//     {
//         if (!mesh || polys.empty())
//             return;

//         float w = std::abs(amount);
//         if (un::is_zero(w))
//             return;

//         // ------------------------------------------------------------
//         // Build poly groups
//         // ------------------------------------------------------------
//         std::vector<std::vector<int32_t>> polyGroups;

//         if (group)
//         {
//             polyGroups.emplace_back(polys.begin(), polys.end());
//         }
//         else
//         {
//             for (int32_t p : polys)
//                 polyGroups.push_back({p});
//         }

//         // ------------------------------------------------------------
//         // For each group, bevel its boundary edges
//         // ------------------------------------------------------------
//         for (const auto& grp : polyGroups)
//         {
//             std::unordered_set<int32_t> grpSet(grp.begin(), grp.end());

//             std::vector<IndexPair> boundaryEdges;
//             boundaryEdges.reserve(grp.size() * 4);

//             for (int32_t p : grp)
//             {
//                 if (!mesh->poly_valid(p))
//                     continue;

//                 const SysPolyEdges edges = mesh->poly_edges(p);

//                 for (const IndexPair& e : edges)
//                 {
//                     const SysEdgePolys adj = mesh->edge_polys(e);

//                     int count = 0;
//                     for (int32_t q : adj)
//                     {
//                         if (q >= 0 && grpSet.contains(q))
//                             ++count;
//                     }

//                     if (count == 1)
//                         boundaryEdges.push_back(e);
//                 }
//             }

//             if (!boundaryEdges.empty())
//                 bevelEdges(mesh, boundaryEdges, w);
//         }
//     }

//     void bevelVerts(SysMesh* mesh, std::span<const int32_t> verts, float width)
//     {
//         if (!mesh || verts.empty())
//             return;

//         float w = width;
//         if (w < 0.0f)
//             w = -w;

//         if (un::is_zero(w))
//             return;

//         // ------------------------------------------------------------
//         // Helpers
//         // ------------------------------------------------------------
//         auto sort_edge = [](IndexPair e) noexcept -> IndexPair {
//             if (e.first > e.second)
//                 std::swap(e.first, e.second);
//             return e;
//         };

//         auto safe_normalize = [](const glm::vec3& v) noexcept -> glm::vec3 {
//             const float d2 = glm::dot(v, v);
//             if (d2 < 1e-20f)
//                 return glm::vec3(0.0f);
//             return v / std::sqrt(d2);
//         };

//         auto find_in_ring = [](const SysPolyVerts& pv, int32_t v) noexcept -> int {
//             for (int i = 0; i < pv.size(); ++i)
//             {
//                 if (pv[i] == v)
//                     return i;
//             }
//             return -1;
//         };

//         auto cleanup_ring = [](SysPolyVerts& pv) noexcept {
//             if (pv.size() < 2)
//                 return;

//             SysPolyVerts out;
//             out.reserve(pv.size());

//             for (int i = 0; i < pv.size(); ++i)
//             {
//                 const int32_t a = pv[i];
//                 if (out.empty() || out.back() != a)
//                     out.push_back(a);
//             }

//             if (out.size() >= 2 && out.front() == out.back())
//                 out.pop_back();

//             pv = out;
//         };

//         struct NewPoly
//         {
//             SysPolyVerts verts    = {};
//             uint32_t     material = 0;
//         };

//         // If multiple selected verts touch the same poly, avoid rebuilding that poly twice.
//         std::unordered_set<int32_t> polysAlreadyRebuilt;
//         polysAlreadyRebuilt.reserve(2048);

//         for (int32_t v : verts)
//         {
//             if (v < 0 || !mesh->vert_valid(v))
//                 continue;

//             const glm::vec3 P = mesh->vert_position(v);

//             // Incident polys
//             const SysVertPolys vpAll = mesh->vert_polys(v);
//             if (vpAll.size() < 2)
//                 continue;

//             std::vector<int32_t> incidentPolys;
//             incidentPolys.reserve(vpAll.size());

//             std::unordered_set<int32_t> incidentSet;
//             incidentSet.reserve(vpAll.size() * 2u);

//             for (int32_t p : vpAll)
//             {
//                 if (p < 0 || !mesh->poly_valid(p))
//                     continue;
//                 if (polysAlreadyRebuilt.contains(p))
//                     continue;

//                 const SysPolyVerts& pv = mesh->poly_verts(p);
//                 if (find_in_ring(pv, v) < 0)
//                     continue;

//                 incidentPolys.push_back(p);
//                 incidentSet.insert(p);
//             }

//             if (incidentPolys.size() < 2)
//                 continue;

//             // ------------------------------------------------------------
//             // 1) Create one new vertex per incident edge (v, neighbor)
//             //    Also build the local "fan graph" around v from incident polys:
//             //
//             //        prevNeighbor -> nextNeighbor   (at v in poly winding)
//             //
//             //    This gives a stable topological order for the cap.
//             // ------------------------------------------------------------
//             std::unordered_map<int32_t, int32_t> edgeVert; // neighbor -> newVertId
//             edgeVert.reserve(incidentPolys.size() * 2u);

//             std::unordered_map<int32_t, int32_t> nextOf; // prevNeighbor -> nextNeighbor
//             nextOf.reserve(incidentPolys.size() * 2u);

//             std::unordered_map<int32_t, int32_t> prevOf; // nextNeighbor -> prevNeighbor
//             prevOf.reserve(incidentPolys.size() * 2u);

//             // Track boundary edges in the *local fan*: edges (v,neighbor) that have only
//             // one incident poly within incidentSet -> open fan end(s).
//             std::unordered_set<int32_t> localBoundaryNeighbors;
//             localBoundaryNeighbors.reserve(8);

//             auto ensure_edge_vert = [&](int32_t neighbor) {
//                 if (!mesh->vert_valid(neighbor))
//                     return;

//                 if (edgeVert.contains(neighbor))
//                     return;

//                 const glm::vec3 dir = safe_normalize(mesh->vert_position(neighbor) - P);
//                 const int32_t   nv  = mesh->create_vert(P + dir * w);
//                 edgeVert.emplace(neighbor, nv);
//             };

//             for (int32_t p : incidentPolys)
//             {
//                 const SysPolyVerts& pv = mesh->poly_verts(p);
//                 const int           n  = pv.size();
//                 if (n < 3)
//                     continue;

//                 const int i = find_in_ring(pv, v);
//                 if (i < 0)
//                     continue;

//                 const int32_t vPrev = pv[(i + n - 1) % n];
//                 const int32_t vNext = pv[(i + 1) % n];

//                 ensure_edge_vert(vPrev);
//                 ensure_edge_vert(vNext);

//                 // Build prev->next mapping for fan traversal.
//                 // If non-manifold around v, these might collide; we keep first and move on.
//                 if (!nextOf.contains(vPrev))
//                     nextOf.emplace(vPrev, vNext);

//                 if (!prevOf.contains(vNext))
//                     prevOf.emplace(vNext, vPrev);
//             }

//             if (edgeVert.size() < 3)
//                 continue;

//             // Detect local boundary neighbors by checking how many incident polys in incidentSet
//             // use edge (v, neighbor).
//             //
//             // Because SysMesh::edge_polys scans vert.polys of edge.first,
//             // we pass edge.first=v so it checks v's adjacency list.
//             for (const auto& [neighbor, newVertId] : edgeVert)
//             {
//                 (void)newVertId;

//                 const SysEdgePolys adj     = mesh->edge_polys(IndexPair(v, neighbor));
//                 int                inCount = 0;
//                 for (int32_t q : adj)
//                 {
//                     if (q >= 0 && mesh->poly_valid(q) && incidentSet.contains(q))
//                         ++inCount;
//                 }

//                 if (inCount == 1)
//                     localBoundaryNeighbors.insert(neighbor);
//             }

//             const bool openFan = !localBoundaryNeighbors.empty();

//             // ------------------------------------------------------------
//             // 2) Rebuild each incident poly: replace v with [new(prev), new(next)]
//             // ------------------------------------------------------------
//             std::vector<NewPoly> rebuilt;
//             rebuilt.reserve(incidentPolys.size());

//             for (int32_t p : incidentPolys)
//             {
//                 const SysPolyVerts& pv = mesh->poly_verts(p);
//                 const int           n  = pv.size();
//                 if (n < 3)
//                     continue;

//                 const int i = find_in_ring(pv, v);
//                 if (i < 0)
//                     continue;

//                 const int32_t vPrev = pv[(i + n - 1) % n];
//                 const int32_t vNext = pv[(i + 1) % n];

//                 auto itA = edgeVert.find(vPrev);
//                 auto itB = edgeVert.find(vNext);
//                 if (itA == edgeVert.end() || itB == edgeVert.end())
//                     continue;

//                 const int32_t a2 = itA->second;
//                 const int32_t b2 = itB->second;

//                 SysPolyVerts out;
//                 out.reserve(n + 1);

//                 for (int k = 0; k < i; ++k)
//                     out.push_back(pv[k]);

//                 out.push_back(a2);
//                 out.push_back(b2);

//                 for (int k = i + 1; k < n; ++k)
//                     out.push_back(pv[k]);

//                 cleanup_ring(out);

//                 if (out.size() < 3)
//                     continue;

//                 NewPoly np  = {};
//                 np.verts    = out;
//                 np.material = mesh->poly_material(p);
//                 rebuilt.push_back(std::move(np));
//             }

//             if (rebuilt.empty())
//                 continue;

//             // ------------------------------------------------------------
//             // 3) Build CAP ring by topological fan traversal (stable)
//             //
//             // Closed fan:
//             //   pick any startPrev, follow prev->next until it loops.
//             //
//             // Open fan:
//             //   pick a startPrev that has no prevOf (start of chain),
//             //   follow prev->next until it ends.
//             //
//             // For open fan, we *skip* creating the cap polygon (for now),
//             // because a "closed cap" is invalid there.
//             // (We can add wedge-closure later if desired.)
//             // ------------------------------------------------------------
//             SysPolyVerts cap = {};

//             if (!openFan)
//             {
//                 // Find a startPrev:
//                 int32_t startPrev = -1;

//                 // In a closed manifold fan, every neighbor appears in both maps.
//                 // Pick any key from nextOf.
//                 if (!nextOf.empty())
//                     startPrev = nextOf.begin()->first;

//                 // Traverse cycle:
//                 if (startPrev >= 0)
//                 {
//                     int32_t curPrev = startPrev;

//                     // We push the "next" neighbor each step:
//                     // cap verts are edgeVert[nextNeighbor] in cyclic order.
//                     // Add a step guard to avoid infinite loops on bad topology.
//                     const int maxSteps = int(edgeVert.size()) + 8;

//                     for (int step = 0; step < maxSteps; ++step)
//                     {
//                         auto it = nextOf.find(curPrev);
//                         if (it == nextOf.end())
//                             break;

//                         const int32_t curNext = it->second;

//                         auto itV = edgeVert.find(curNext);
//                         if (itV != edgeVert.end())
//                             cap.push_back(itV->second);

//                         curPrev = curNext;

//                         if (curPrev == startPrev)
//                             break;
//                     }

//                     cleanup_ring(cap);
//                 }
//             }

//             // ------------------------------------------------------------
//             // 4) Apply: remove old incident polys, add rebuilt, add cap (if any)
//             // ------------------------------------------------------------
//             for (int32_t p : incidentPolys)
//             {
//                 if (mesh->poly_valid(p))
//                 {
//                     polysAlreadyRebuilt.insert(p);
//                     mesh->remove_poly(p);
//                 }
//             }

//             for (const NewPoly& np : rebuilt)
//             {
//                 if (np.verts.size() >= 3)
//                     mesh->create_poly(np.verts, np.material);
//             }

//             if (cap.size() >= 3)
//             {
//                 uint32_t capMat = mesh->poly_material(incidentPolys.front());
//                 mesh->create_poly(cap, capMat);
//             }

//             // NOTE:
//             // We do not remove original vertex v. If we want cleanup later:
//             // - if mesh->vert_polys(v).empty() -> remove_vert(v)
//         }
//     }

// } // namespace ops::sys

// namespace ops::he
// {
//     void bevelEdges(SysMesh* mesh, std::span<const IndexPair> edges, float width)
//     {
//         if (!mesh || edges.empty())
//             return;

//         float w = width;
//         if (w < 0.0f)
//             w = -w;

//         if (un::is_zero(w))
//             return;

//         // =========================================================
//         // 1) Normalize / unique selected Sys edges
//         // =========================================================
//         std::vector<IndexPair> selEdges;
//         selEdges.reserve(edges.size());

//         std::unordered_set<uint64_t> selEdgeSet;
//         selEdgeSet.reserve(edges.size() * 2u);

//         for (const IndexPair& e0 : edges)
//         {
//             if (e0.first < 0 || e0.second < 0)
//                 continue;
//             if (e0.first == e0.second)
//                 continue;
//             if (!mesh->vert_valid(e0.first) || !mesh->vert_valid(e0.second))
//                 continue;

//             const IndexPair e = mesh->sort_edge(e0);
//             const uint64_t  k = un::pack_undirected_i32(e.first, e.second);

//             if (!selEdgeSet.insert(k).second)
//                 continue;

//             selEdges.push_back(e);
//         }

//         if (selEdges.empty())
//             return;

//         // =========================================================
//         // 2) Editable Sys polys = all polys incident to selected edges
//         // =========================================================
//         std::vector<int32_t> editableSysPolys;
//         editableSysPolys.reserve(selEdges.size() * 2u);

//         std::unordered_set<int32_t> editableSysSet;
//         editableSysSet.reserve(selEdges.size() * 4u);

//         for (const IndexPair& e : selEdges)
//         {
//             const SysEdgePolys adj = mesh->edge_polys(e);
//             for (int32_t p : adj)
//             {
//                 if (p < 0 || !mesh->poly_valid(p))
//                     continue;

//                 if (editableSysSet.insert(p).second)
//                     editableSysPolys.push_back(p);
//             }
//         }

//         if (editableSysPolys.empty())
//             return;

//         // =========================================================
//         // 3) Extract to HeMesh (include boundary neighbors for context)
//         // =========================================================
//         HeExtractionOptions opt      = {};
//         opt.includeBoundaryNeighbors = true;
//         opt.importNormals            = true;
//         opt.importUVs                = true;
//         opt.normalMapId              = 0;
//         opt.uvMapId                  = 1;

//         HeExtractionResult ex =
//             extract_polys_to_hemesh(mesh,
//                                     std::span<const int32_t>(editableSysPolys.data(), editableSysPolys.size()),
//                                     opt);

//         if (ex.editableSysPolys.empty())
//             return;

//         HeMesh& hem = ex.mesh;

//         auto sys_to_he_vert = [&](int32_t sysV) noexcept -> HeMesh::VertId {
//             if (sysV < 0)
//                 return HeMesh::kInvalidVert;
//             if (sysV >= static_cast<int32_t>(ex.sysVertToHeVert.size()))
//                 return HeMesh::kInvalidVert;
//             return static_cast<HeMesh::VertId>(ex.sysVertToHeVert[sysV]);
//         };

//         auto he_poly_is_editable = [&](HeMesh::PolyId hp) noexcept -> bool {
//             if (!hem.polyValid(hp))
//                 return false;
//             if (hp < 0)
//                 return false;
//             if (static_cast<size_t>(hp) >= ex.hePolyEditable.size())
//                 return false;
//             return ex.hePolyEditable[static_cast<size_t>(hp)] != 0;
//         };

//         // =========================================================
//         // 4) Translate selected Sys edges -> unique selected He edges
//         // =========================================================
//         std::vector<HeMesh::EdgeId> heSelEdges;
//         heSelEdges.reserve(selEdges.size());

//         std::unordered_set<uint64_t> heSelEdgeSet;
//         heSelEdgeSet.reserve(selEdges.size() * 2u);

//         for (const IndexPair& se : selEdges)
//         {
//             const HeMesh::VertId ha = sys_to_he_vert(se.first);
//             const HeMesh::VertId hb = sys_to_he_vert(se.second);

//             if (ha == HeMesh::kInvalidVert || hb == HeMesh::kInvalidVert || ha == hb)
//                 continue;

//             const HeMesh::EdgeId he = hem.findEdge(ha, hb);
//             if (he == HeMesh::kInvalidEdge || !hem.edgeValid(he))
//                 continue;

//             const uint64_t k = un::pack_undirected_i32(ha, hb);
//             if (!heSelEdgeSet.insert(k).second)
//                 continue;

//             heSelEdges.push_back(he);
//         }

//         if (heSelEdges.empty())
//             return;

//         auto is_sel_edge = [&](HeMesh::VertId a, HeMesh::VertId b) noexcept -> bool {
//             return heSelEdgeSet.contains(un::pack_undirected_i32(a, b));
//         };

//         // =========================================================
//         // 5) Cache incident polys for each selected He edge BEFORE edits
//         // =========================================================
//         struct HeEdgeInfo
//         {
//             HeMesh::VertId              a{HeMesh::kInvalidVert};
//             HeMesh::VertId              b{HeMesh::kInvalidVert};
//             std::vector<HeMesh::PolyId> polys{};
//         };

//         std::vector<HeEdgeInfo> heEdgeInfos;
//         heEdgeInfos.reserve(heSelEdges.size());

//         for (HeMesh::EdgeId e : heSelEdges)
//         {
//             if (!hem.edgeValid(e))
//                 continue;

//             auto [a, b] = hem.edgeVerts(e);

//             HeEdgeInfo info{};
//             info.a = a;
//             info.b = b;

//             for (HeMesh::PolyId p : hem.edgePolys(e))
//                 info.polys.push_back(p);

//             heEdgeInfos.push_back(std::move(info));
//         }

//         // =========================================================
//         // 6) Poly groups: flood fill within editable across NON-selected edges
//         // =========================================================
//         std::unordered_map<int32_t, int32_t> polyGroup; // PolyId -> groupId
//         polyGroup.reserve(static_cast<size_t>(hem.polyCount()) * 2u);

//         int32_t nextGroup = 0;

//         for (HeMesh::PolyId seed : hem.allPolys())
//         {
//             if (!he_poly_is_editable(seed))
//                 continue;

//             if (polyGroup.contains(seed))
//                 continue;

//             const int32_t gid = nextGroup++;
//             polyGroup.emplace(seed, gid);

//             std::vector<HeMesh::PolyId> stack;
//             stack.push_back(seed);

//             while (!stack.empty())
//             {
//                 const HeMesh::PolyId p = stack.back();
//                 stack.pop_back();

//                 if (!he_poly_is_editable(p))
//                     continue;

//                 const auto pv = hem.polyVerts(p);
//                 const int  n  = static_cast<int>(pv.size());
//                 if (n < 3)
//                     continue;

//                 for (int i = 0; i < n; ++i)
//                 {
//                     const HeMesh::VertId a = pv[i];
//                     const HeMesh::VertId b = pv[(i + 1) % n];

//                     if (is_sel_edge(a, b))
//                         continue; // selected edges separate groups

//                     const HeMesh::EdgeId e = hem.findEdge(a, b);
//                     if (e == HeMesh::kInvalidEdge || !hem.edgeValid(e))
//                         continue;

//                     for (HeMesh::PolyId q : hem.edgePolys(e))
//                     {
//                         if (q == p)
//                             continue;
//                         if (!he_poly_is_editable(q))
//                             continue;
//                         if (polyGroup.contains(q))
//                             continue;

//                         polyGroup.emplace(q, gid);
//                         stack.push_back(q);
//                     }
//                 }
//             }
//         }

//         // =========================================================
//         // 7) Shared inset verts per (groupId, originalVert) via ACCUMULATED samples
//         // =========================================================
//         struct InsetKey
//         {
//             int32_t        g{};
//             HeMesh::VertId v{};

//             bool operator==(const InsetKey& o) const noexcept
//             {
//                 return g == o.g && v == o.v;
//             }
//         };

//         struct InsetKeyHash
//         {
//             std::size_t operator()(const InsetKey& k) const noexcept
//             {
//                 std::size_t h1 = std::hash<int32_t>{}(k.g);
//                 std::size_t h2 = std::hash<int32_t>{}(k.v);
//                 return h1 ^ (h2 + 0x9e3779b9u + (h1 << 6) + (h1 >> 2));
//             }
//         };

//         struct Accum
//         {
//             glm::vec3 sum{0.0f};
//             int32_t   count{0};
//         };

//         std::unordered_map<InsetKey, Accum, InsetKeyHash> insetAccum;
//         insetAccum.reserve(4096);

//         auto add_inset_sample = [&](int32_t gid, HeMesh::VertId v, const glm::vec3& pos) {
//             InsetKey k{gid, v};
//             auto&    a = insetAccum[k];
//             a.sum += pos;
//             a.count += 1;
//         };

//         auto poly_centroid_he = [&](HeMesh::PolyId p) noexcept -> glm::vec3 {
//             const auto pv = hem.polyVerts(p);
//             glm::vec3  c(0.0f);
//             int32_t    n = 0;
//             for (HeMesh::VertId v : pv)
//             {
//                 if (!hem.vertValid(v))
//                     continue;
//                 c += hem.position(v);
//                 ++n;
//             }
//             return (n > 0) ? (c / float(n)) : glm::vec3(0.0f);
//         };

//         // Winding-robust inward direction on He poly p for directed edge (v0 -> v1)
//         auto inward_dir_he = [&](HeMesh::PolyId p, HeMesh::VertId v0, HeMesh::VertId v1) noexcept -> glm::vec3 {
//             const glm::vec3 N = hem.polyNormal(p);
//             if (glm::dot(N, N) < 1e-12f)
//                 return glm::vec3(0.0f);

//             const glm::vec3 P0 = hem.position(v0);
//             const glm::vec3 P1 = hem.position(v1);

//             glm::vec3 d = un::safe_normalize(P1 - P0);
//             if (glm::dot(d, d) < 1e-12f)
//                 return glm::vec3(0.0f);

//             glm::vec3 in = un::safe_normalize(glm::cross(N, d));
//             if (glm::dot(in, in) < 1e-12f)
//                 return glm::vec3(0.0f);

//             // Flip if it points away from interior (centroid test)
//             const glm::vec3 C   = poly_centroid_he(p);
//             const glm::vec3 mid = 0.5f * (P0 + P1);
//             if (glm::dot(in, (C - mid)) < 0.0f)
//                 in = -in;

//             return in;
//         };

//         for (HeMesh::PolyId p : hem.allPolys())
//         {
//             if (!he_poly_is_editable(p))
//                 continue;

//             auto itg = polyGroup.find(p);
//             if (itg == polyGroup.end())
//                 continue;

//             const int32_t gid = itg->second;

//             const auto pv = hem.polyVerts(p);
//             const int  n  = static_cast<int>(pv.size());
//             if (n < 3)
//                 continue;

//             const glm::vec3 N = hem.polyNormal(p);
//             if (glm::dot(N, N) < 1e-12f)
//                 continue;

//             glm::vec3 U(0.0f), V(0.0f);
//             un::make_basis(N, U, V);

//             for (int i = 0; i < n; ++i)
//             {
//                 const HeMesh::VertId vPrev = pv[(i + n - 1) % n];
//                 const HeMesh::VertId v     = pv[i];
//                 const HeMesh::VertId vNext = pv[(i + 1) % n];

//                 const bool selIn  = is_sel_edge(vPrev, v);
//                 const bool selOut = is_sel_edge(v, vNext);

//                 if (!selIn && !selOut)
//                     continue;

//                 const glm::vec3 P = hem.position(v);

//                 struct Line2
//                 {
//                     glm::vec2 p{}, d{};
//                     bool      valid{false};
//                 };

//                 Line2 L0{}, L1{};

//                 if (selIn)
//                 {
//                     const glm::vec3 in  = inward_dir_he(p, vPrev, v);
//                     const glm::vec3 p0w = P + in * w;
//                     const glm::vec3 d0w = un::safe_normalize(P - hem.position(vPrev));

//                     L0.p     = glm::vec2(glm::dot(p0w, U), glm::dot(p0w, V));
//                     L0.d     = glm::vec2(glm::dot(d0w, U), glm::dot(d0w, V));
//                     L0.valid = true;
//                 }

//                 if (selOut)
//                 {
//                     const glm::vec3 in  = inward_dir_he(p, v, vNext);
//                     const glm::vec3 p1w = P + in * w;
//                     const glm::vec3 d1w = un::safe_normalize(hem.position(vNext) - P);

//                     if (!L0.valid)
//                     {
//                         L0.p     = glm::vec2(glm::dot(p1w, U), glm::dot(p1w, V));
//                         L0.d     = glm::vec2(glm::dot(d1w, U), glm::dot(d1w, V));
//                         L0.valid = true;
//                     }
//                     else
//                     {
//                         L1.p     = glm::vec2(glm::dot(p1w, U), glm::dot(p1w, V));
//                         L1.d     = glm::vec2(glm::dot(d1w, U), glm::dot(d1w, V));
//                         L1.valid = true;
//                     }
//                 }

//                 glm::vec3 newPos = P;

//                 if (L0.valid && L1.valid)
//                 {
//                     glm::vec2 isect{};
//                     if (un::intersect_lines_2d(L0.p, L0.d, L1.p, L1.d, isect))
//                     {
//                         const float h = glm::dot(P, N);
//                         newPos        = isect.x * U + isect.y * V + h * N;
//                     }
//                     else
//                     {
//                         glm::vec3 inSum(0.0f);
//                         if (selIn)
//                             inSum += inward_dir_he(p, vPrev, v);
//                         if (selOut)
//                             inSum += inward_dir_he(p, v, vNext);

//                         inSum = un::safe_normalize(inSum);
//                         if (glm::dot(inSum, inSum) > 0.0f)
//                             newPos = P + inSum * w;
//                     }
//                 }
//                 else if (L0.valid)
//                 {
//                     glm::vec3 inSum(0.0f);
//                     if (selIn)
//                         inSum += inward_dir_he(p, vPrev, v);
//                     if (selOut)
//                         inSum += inward_dir_he(p, v, vNext);

//                     inSum = un::safe_normalize(inSum);
//                     if (glm::dot(inSum, inSum) > 0.0f)
//                         newPos = P + inSum * w;
//                 }

//                 add_inset_sample(gid, v, newPos);
//             }
//         }

//         // Materialize inset verts: (gid, v) -> new He vert
//         std::unordered_map<InsetKey, HeMesh::VertId, InsetKeyHash> insetVert;
//         insetVert.reserve(insetAccum.size() * 2u);

//         auto inset_for = [&](int32_t gid, HeMesh::VertId v) noexcept -> HeMesh::VertId {
//             const InsetKey k{gid, v};
//             auto           it = insetVert.find(k);
//             return (it != insetVert.end()) ? it->second : HeMesh::kInvalidVert;
//         };

//         for (const auto& [k, a] : insetAccum)
//         {
//             if (a.count <= 0)
//                 continue;

//             const glm::vec3 pos    = a.sum / float(a.count);
//             const auto      vInset = hem.createVert(pos);
//             insetVert.emplace(k, vInset);
//         }

//         if (insetVert.empty())
//             return;

//         // =========================================================
//         // 8) Rebuild editable polys: swap touched corners to inset_for(gid, v)
//         // =========================================================
//         for (HeMesh::PolyId p : hem.allPolys())
//         {
//             if (!he_poly_is_editable(p))
//                 continue;

//             auto itg = polyGroup.find(p);
//             if (itg == polyGroup.end())
//                 continue;

//             const int32_t gid = itg->second;

//             const auto pv = hem.polyVerts(p);
//             const int  n  = static_cast<int>(pv.size());
//             if (n < 3)
//                 continue;

//             std::vector<HeMesh::VertId> newRing;
//             newRing.reserve(pv.size());

//             for (int i = 0; i < n; ++i)
//             {
//                 const HeMesh::VertId vPrev = pv[(i + n - 1) % n];
//                 const HeMesh::VertId v     = pv[i];
//                 const HeMesh::VertId vNext = pv[(i + 1) % n];

//                 const bool touches = is_sel_edge(vPrev, v) || is_sel_edge(v, vNext);

//                 if (touches)
//                 {
//                     const HeMesh::VertId vi = inset_for(gid, v);
//                     newRing.push_back((vi != HeMesh::kInvalidVert) ? vi : v);
//                 }
//                 else
//                 {
//                     newRing.push_back(v);
//                 }
//             }

//             hem.setPolyVerts(p, newRing);
//         }

//         // =========================================================
//         // X) Stitch end-of-selection neighbor polys (edge-free)
//         //    For each editable boundary edge (vInset, n):
//         //      - find original vertex vOrig that generated vInset
//         //      - among non-editable polys incident to n, find the one that also contains vOrig
//         //      - replace vOrig -> vInset in that poly ring
//         // =========================================================
//         {
//             struct InsetRev
//             {
//                 int32_t        gid  = -1;
//                 HeMesh::VertId orig = HeMesh::kInvalidVert;
//             };

//             std::unordered_map<HeMesh::VertId, InsetRev> insetToOrig;
//             insetToOrig.reserve(insetVert.size() * 2u);
//             for (const auto& [k, vInset] : insetVert)
//                 insetToOrig.emplace(vInset, InsetRev{k.g, k.v});

//             auto poly_contains = [&](HeMesh::PolyId p, HeMesh::VertId v) -> bool {
//                 const auto pv = hem.polyVerts(p);
//                 for (HeMesh::VertId x : pv)
//                     if (x == v)
//                         return true;
//                 return false;
//             };

//             auto replace_in_poly = [&](HeMesh::PolyId p, HeMesh::VertId fromV, HeMesh::VertId toV) -> bool {
//                 if (!hem.polyValid(p) || !hem.vertValid(fromV) || !hem.vertValid(toV) || fromV == toV)
//                     return false;

//                 const auto pv = hem.polyVerts(p);
//                 if (pv.size() < 3)
//                     return false;

//                 bool changed = false;

//                 std::vector<HeMesh::VertId> ring;
//                 ring.reserve(pv.size());

//                 for (HeMesh::VertId v : pv)
//                 {
//                     if (v == fromV)
//                     {
//                         ring.push_back(toV);
//                         changed = true;
//                     }
//                     else
//                     {
//                         ring.push_back(v);
//                     }
//                 }

//                 if (!changed)
//                     return false;

//                 // collapse consecutive duplicates (including wrap)
//                 auto collapse_dupes = [&](std::vector<HeMesh::VertId>& r) {
//                     if (r.size() < 3)
//                         return;

//                     std::vector<HeMesh::VertId> tmp;
//                     tmp.reserve(r.size());

//                     for (HeMesh::VertId v : r)
//                     {
//                         if (tmp.empty() || tmp.back() != v)
//                             tmp.push_back(v);
//                     }

//                     if (tmp.size() >= 2 && tmp.front() == tmp.back())
//                         tmp.pop_back();
//                     if (tmp.size() >= 2 && tmp.front() == tmp.back())
//                         tmp.pop_back();

//                     r.swap(tmp);
//                 };

//                 collapse_dupes(ring);

//                 if (ring.size() < 3)
//                     return false;

//                 hem.setPolyVerts(p, ring);
//                 return true;
//             };

//             // Walk all boundary edges that belong to editable polys (open boundary after rebuild)
//             for (HeMesh::EdgeId e : hem.allEdges())
//             {
//                 if (!hem.edgeValid(e))
//                     continue;

//                 const auto ep = hem.edgePolys(e);
//                 if (ep.size() != 1)
//                     continue; // not boundary

//                 const HeMesh::PolyId pEdit = ep[0];
//                 if (!hem.polyValid(pEdit) || !he_poly_is_editable(pEdit))
//                     continue;

//                 const auto [a, b] = hem.edgeVerts(e);
//                 if (!hem.vertValid(a) || !hem.vertValid(b))
//                     continue;

//                 const auto ita = insetToOrig.find(a);
//                 const auto itb = insetToOrig.find(b);

//                 const bool aIsInset = (ita != insetToOrig.end());
//                 const bool bIsInset = (itb != insetToOrig.end());

//                 if (aIsInset == bIsInset)
//                     continue; // both inset (cap pass handles) or both original

//                 const HeMesh::VertId vInset = aIsInset ? a : b;
//                 const HeMesh::VertId n      = aIsInset ? b : a;

//                 const HeMesh::VertId vOrig = aIsInset ? ita->second.orig : itb->second.orig;
//                 if (!hem.vertValid(vOrig) || !hem.vertValid(n))
//                     continue;

//                 // Find the unique non-editable poly around vertex n that still uses vOrig
//                 HeMesh::PolyId target = HeMesh::kInvalidPoly;

//                 for (HeMesh::PolyId q : hem.vertPolys(n))
//                 {
//                     if (!hem.polyValid(q))
//                         continue;

//                     if (he_poly_is_editable(q))
//                         continue;

//                     if (!poly_contains(q, vOrig))
//                         continue;

//                     target = q;
//                     break;
//                 }

//                 if (target == HeMesh::kInvalidPoly)
//                     continue;

//                 replace_in_poly(target, vOrig, vInset);
//             }
//         }

//         // =========================================================
//         // 9) Band quads across each manifold selected edge (same intent as SysMesh version)
//         // =========================================================
//         {
//             std::unordered_set<uint64_t> bridged;
//             bridged.reserve(heEdgeInfos.size() * 2u);

//             for (const HeEdgeInfo& info : heEdgeInfos)
//             {
//                 HeMesh::PolyId p = HeMesh::kInvalidPoly;
//                 HeMesh::PolyId q = HeMesh::kInvalidPoly;

//                 for (HeMesh::PolyId pid : info.polys)
//                 {
//                     if (!he_poly_is_editable(pid))
//                         continue;

//                     if (p == HeMesh::kInvalidPoly)
//                         p = pid;
//                     else
//                     {
//                         q = pid;
//                         break;
//                     }
//                 }

//                 if (p == HeMesh::kInvalidPoly || q == HeMesh::kInvalidPoly)
//                     continue;

//                 const uint64_t ek = un::pack_undirected_i32(info.a, info.b);
//                 if (bridged.contains(ek))
//                     continue;

//                 const int32_t gp = polyGroup.contains(p) ? polyGroup[p] : -1;
//                 const int32_t gq = polyGroup.contains(q) ? polyGroup[q] : -1;
//                 if (gp < 0 || gq < 0)
//                     continue;

//                 const HeMesh::VertId a2p = inset_for(gp, info.a);
//                 const HeMesh::VertId b2p = inset_for(gp, info.b);
//                 const HeMesh::VertId a2q = inset_for(gq, info.a);
//                 const HeMesh::VertId b2q = inset_for(gq, info.b);

//                 if (!hem.vertValid(a2p) || !hem.vertValid(b2p) || !hem.vertValid(a2q) || !hem.vertValid(b2q))
//                     continue;

//                 std::vector<HeMesh::VertId> band{a2p, b2p, b2q, a2q};

//                 // Winding sanity (same as your SysMesh band quad check)
//                 {
//                     glm::vec3 N = un::safe_normalize(hem.polyNormal(p) + hem.polyNormal(q));

//                     const glm::vec3 P0 = hem.position(band[0]);
//                     const glm::vec3 P1 = hem.position(band[1]);
//                     const glm::vec3 P2 = hem.position(band[2]);

//                     glm::vec3 Nb = un::safe_normalize(glm::cross(P1 - P0, P2 - P0));

//                     if (glm::dot(Nb, N) < 0.0f)
//                         std::swap(band[1], band[3]);
//                 }

//                 hem.createPoly(band, hem.polyMaterial(p));
//                 bridged.insert(ek);
//             }
//         }
//         // =========================================================
//         // 11) Cap bevel-created holes (boundary loops of inset verts)
//         // =========================================================
//         {
//             std::unordered_set<HeMesh::VertId> insetSet;
//             insetSet.reserve(insetVert.size() * 2u);
//             for (const auto& [k, vInset] : insetVert)
//                 insetSet.insert(vInset);

//             // Gather boundary edges whose endpoints are BOTH inset verts
//             std::vector<HeMesh::EdgeId> boundaryEdges;
//             boundaryEdges.reserve(256);

//             for (HeMesh::EdgeId e : hem.allEdges())
//             {
//                 if (!hem.edgeValid(e))
//                     continue;

//                 const auto polys = hem.edgePolys(e);
//                 if (polys.size() != 1)
//                     continue;

//                 const auto [a, b] = hem.edgeVerts(e);
//                 if (!hem.vertValid(a) || !hem.vertValid(b))
//                     continue;

//                 // Only cap holes formed by bevel geometry (new inset boundary)
//                 if (!insetSet.contains(a) || !insetSet.contains(b))
//                     continue;

//                 boundaryEdges.push_back(e);
//             }

//             if (!boundaryEdges.empty())
//             {
//                 auto pack_edge = [](int32_t a, int32_t b) noexcept -> uint64_t {
//                     return un::pack_undirected_i32(a, b);
//                 };

//                 // Build boundary adjacency (vertex -> boundary neighbors)
//                 std::unordered_map<HeMesh::VertId, std::vector<HeMesh::VertId>> nbrs;
//                 nbrs.reserve(boundaryEdges.size() * 2u);

//                 std::unordered_set<uint64_t> boundaryEdgeSet;
//                 boundaryEdgeSet.reserve(boundaryEdges.size() * 2u);

//                 for (HeMesh::EdgeId e : boundaryEdges)
//                 {
//                     auto [a, b] = hem.edgeVerts(e);
//                     nbrs[a].push_back(b);
//                     nbrs[b].push_back(a);
//                     boundaryEdgeSet.insert(pack_edge(a, b));
//                 }

//                 std::unordered_set<uint64_t> visited;
//                 visited.reserve(boundaryEdges.size() * 2u);

//                 auto pick_material_from_edge = [&](HeMesh::VertId a, HeMesh::VertId b) -> uint32_t {
//                     const HeMesh::EdgeId e = hem.findEdge(a, b);
//                     if (e == HeMesh::kInvalidEdge || !hem.edgeValid(e))
//                         return 0;

//                     const auto ep = hem.edgePolys(e);
//                     if (ep.empty())
//                         return 0;

//                     const HeMesh::PolyId p = ep[0];
//                     return hem.polyValid(p) ? hem.polyMaterial(p) : 0;
//                 };

//                 // Walk loops
//                 for (const auto& [startV, _] : nbrs)
//                 {
//                     // We start loop walking from each vertex but only if it has unvisited outgoing edge.
//                     // Find an unvisited edge incident to startV.
//                     bool hasUnvisited = false;
//                     for (HeMesh::VertId n : nbrs[startV])
//                     {
//                         const uint64_t k = pack_edge(startV, n);
//                         if (boundaryEdgeSet.contains(k) && !visited.contains(k))
//                         {
//                             hasUnvisited = true;
//                             break;
//                         }
//                     }
//                     if (!hasUnvisited)
//                         continue;

//                     // Start walking
//                     std::vector<HeMesh::VertId> loop;
//                     loop.reserve(32);

//                     HeMesh::VertId prev = HeMesh::kInvalidVert;
//                     HeMesh::VertId cur  = startV;

//                     // Pick an initial next
//                     HeMesh::VertId next = HeMesh::kInvalidVert;
//                     for (HeMesh::VertId n : nbrs[cur])
//                     {
//                         const uint64_t k = pack_edge(cur, n);
//                         if (!visited.contains(k))
//                         {
//                             next = n;
//                             break;
//                         }
//                     }
//                     if (next == HeMesh::kInvalidVert)
//                         continue;

//                     loop.push_back(cur);

//                     // Walk until close or fail
//                     for (int guard = 0; guard < 4096; ++guard)
//                     {
//                         const uint64_t ek = pack_edge(cur, next);
//                         visited.insert(ek);

//                         prev = cur;
//                         cur  = next;

//                         if (cur == startV)
//                             break;

//                         loop.push_back(cur);

//                         // choose next neighbor (not prev) with unvisited edge if possible
//                         HeMesh::VertId candidate = HeMesh::kInvalidVert;

//                         const auto it = nbrs.find(cur);
//                         if (it == nbrs.end() || it->second.empty())
//                             break;

//                         // Prefer not to go back unless forced
//                         for (HeMesh::VertId n : it->second)
//                         {
//                             if (n == prev)
//                                 continue;
//                             const uint64_t k = pack_edge(cur, n);
//                             if (!visited.contains(k))
//                             {
//                                 candidate = n;
//                                 break;
//                             }
//                         }
//                         if (candidate == HeMesh::kInvalidVert)
//                         {
//                             // If all are visited, try to close back to start
//                             for (HeMesh::VertId n : it->second)
//                             {
//                                 if (n == startV)
//                                 {
//                                     candidate = n;
//                                     break;
//                                 }
//                             }
//                         }

//                         if (candidate == HeMesh::kInvalidVert)
//                             break;

//                         next = candidate;
//                     }

//                     // Closed loop must end at startV, and at least 3 distinct verts
//                     if (loop.size() < 3)
//                         continue;
//                     if (loop.front() != startV)
//                         continue; // shouldn't happen
//                     // We stopped when cur==startV, but loop currently holds startV only once.
//                     // That's what createPoly wants.

//                     // Create cap
//                     const uint32_t mat = pick_material_from_edge(loop[0], loop[1]);
//                     hem.createPoly(loop, mat);
//                 }
//             }
//         }

//         hem.removeUnusedEdges();
//         hem.removeIsolatedVerts();

//         // =========================================================
//         // 10) Commit back: replace ONLY editable Sys polys
//         // =========================================================
//         const HeMeshCommit commit = build_commit_replace_editable(mesh, ex, hem, opt);
//         apply_commit(mesh, ex, commit, opt);
//     }
// } // namespace ops::he

#include "Ops/Bevel.hpp"

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "CoreUtilities.hpp"
#include "SysMesh.hpp"

// ============================================================
// Design notes
// ============================================================
//
// Inset positions are computed with 3D miter vectors, not per-poly
// 2D projected line intersections.
//
// For a corner vertex V touching selected edges in poly P:
//
//   inward(P, A→B)  = normalize( cross(N_P, normalize(B-A)) )
//                     flipped so it points toward the poly interior
//
//   miter = normalize( inward(prev edge) + inward(next edge) )
//
//   miter_len = w / dot(miter, inward(one of the edges))
//               (clamped to avoid exploding at very sharp angles)
//
//   inset_pos = V + miter * miter_len
//
// This is basis-independent, correct for non-planar polys, and
// produces the same position regardless of which poly samples it —
// so averaging across polys that share the same (group, vertex)
// converges to a stable seam-free result.
//
// Band quads are wound by checking whether the computed normal
// agrees with the average of the two adjacent poly normals.
//
// ============================================================

namespace
{
    // ----------------------------------------------------------
    // Inward-pointing unit vector for directed edge (v0 -> v1)
    // inside polygon p. "Inward" means toward the polygon interior.
    // Uses the cross product of the poly normal and the edge direction,
    // then validates against the centroid to handle winding robustly.
    // ----------------------------------------------------------
    static glm::vec3 inward_dir(const SysMesh* mesh,
                                int32_t        p,
                                int32_t        v0,
                                int32_t        v1) noexcept
    {
        const glm::vec3 N = mesh->poly_normal(p);
        if (glm::dot(N, N) < 1e-12f)
            return glm::vec3(0.0f);

        const glm::vec3 edge = un::safe_normalize(mesh->vert_position(v1) -
                                                  mesh->vert_position(v0));
        if (glm::dot(edge, edge) < 1e-12f)
            return glm::vec3(0.0f);

        glm::vec3 in = un::safe_normalize(glm::cross(N, edge));

        // Centroid test: flip if pointing away from interior.
        const glm::vec3 C   = mesh->poly_center(p);
        const glm::vec3 mid = 0.5f * (mesh->vert_position(v0) +
                                      mesh->vert_position(v1));
        if (glm::dot(in, C - mid) < 0.0f)
            in = -in;

        return in;
    }

    // ----------------------------------------------------------
    // Miter position for vertex v in polygon p.
    //
    // selIn  = the edge arriving  at v is selected
    // selOut = the edge departing from v is selected
    // vPrev / vNext = neighbours in the polygon winding
    //
    // Returns the inset position on success, or the original
    // position if the geometry is degenerate.
    // ----------------------------------------------------------
    static glm::vec3 miter_pos(const SysMesh* mesh,
                               int32_t        p,
                               int32_t        vPrev,
                               int32_t        v,
                               int32_t        vNext,
                               bool           selIn,
                               bool           selOut,
                               float          w) noexcept
    {
        const glm::vec3 P = mesh->vert_position(v);

        glm::vec3 inSum(0.0f);
        int       inCount = 0;

        if (selIn)
        {
            glm::vec3 d = inward_dir(mesh, p, vPrev, v);
            if (glm::dot(d, d) > 1e-12f)
            {
                inSum += d;
                ++inCount;
            }
        }
        if (selOut)
        {
            glm::vec3 d = inward_dir(mesh, p, v, vNext);
            if (glm::dot(d, d) > 1e-12f)
            {
                inSum += d;
                ++inCount;
            }
        }

        if (inCount == 0)
            return P;

        const glm::vec3 miter = un::safe_normalize(inSum);
        if (glm::dot(miter, miter) < 1e-12f)
            return P;

        // Reference inward direction for one of the selected edges
        // (used to compute miter scale so the offset is exactly w
        //  perpendicular to the edge).
        glm::vec3 ref(0.0f);
        if (selIn)
            ref = inward_dir(mesh, p, vPrev, v);
        else
            ref = inward_dir(mesh, p, v, vNext);

        float denom = glm::dot(miter, ref);
        if (std::abs(denom) < 1e-6f)
            return P + miter * w; // degenerate: fall back to plain offset

        // Clamp miter scale to avoid exploding at very sharp corners.
        constexpr float kMaxMiterScale = 4.0f;
        const float     scale          = std::min(w / denom, w * kMaxMiterScale);

        return P + miter * scale;
    }

} // anonymous namespace

// ============================================================
namespace ops::sys
{

    // ----------------------------------------------------------
    //  bevelEdges
    // ----------------------------------------------------------
    void bevelEdges(SysMesh* mesh, std::span<const IndexPair> edges, float width)
    {
        if (!mesh || edges.empty())
            return;

        const float w = std::abs(width);
        if (un::is_zero(w))
            return;

        // -------------------------------------------------------
        // 1) Normalize and deduplicate selected edges
        // -------------------------------------------------------
        std::vector<IndexPair>       selEdges;
        std::unordered_set<uint64_t> selEdgeSet;
        selEdges.reserve(edges.size());
        selEdgeSet.reserve(edges.size() * 2u);

        for (const IndexPair& e0 : edges)
        {
            if (e0.first < 0 || e0.second < 0 || e0.first == e0.second)
                continue;
            if (!mesh->vert_valid(e0.first) || !mesh->vert_valid(e0.second))
                continue;

            const IndexPair e = mesh->sort_edge(e0);
            if (selEdgeSet.insert(un::pack_undirected_i32(e.first, e.second)).second)
                selEdges.push_back(e);
        }

        if (selEdges.empty())
            return;

        auto is_sel = [&](int32_t a, int32_t b) noexcept -> bool {
            return selEdgeSet.contains(un::pack_undirected_i32(a, b));
        };

        // -------------------------------------------------------
        // 2) Collect editable polys (incident to selected edges)
        // -------------------------------------------------------
        std::vector<int32_t>        editablePolys;
        std::unordered_set<int32_t> editableSet;
        editablePolys.reserve(selEdges.size() * 2u);
        editableSet.reserve(selEdges.size() * 4u);

        for (const IndexPair& e : selEdges)
        {
            for (int32_t p : mesh->edge_polys(e))
            {
                if (p >= 0 && mesh->poly_valid(p) && editableSet.insert(p).second)
                    editablePolys.push_back(p);
            }
        }

        if (editablePolys.empty())
            return;

        // -------------------------------------------------------
        // 3) Snapshot edge adjacency BEFORE any mutation
        // -------------------------------------------------------
        struct EdgeSnap
        {
            IndexPair            e{};
            std::vector<int32_t> polys{};
        };

        std::vector<EdgeSnap> edgeSnaps;
        edgeSnaps.reserve(selEdges.size());

        for (const IndexPair& e : selEdges)
        {
            EdgeSnap s;
            s.e = e;
            for (int32_t p : mesh->edge_polys(e))
                if (p >= 0 && mesh->poly_valid(p))
                    s.polys.push_back(p);
            edgeSnaps.push_back(std::move(s));
        }

        // -------------------------------------------------------
        // 4) Flood-fill poly groups across NON-selected edges
        //    (polys separated by a selected edge get different groups
        //     so their inset verts are independent)
        // -------------------------------------------------------
        std::unordered_map<int32_t, int32_t> polyGroup;
        polyGroup.reserve(editablePolys.size() * 2u);
        int32_t nextGroup = 0;

        for (int32_t seed : editablePolys)
        {
            if (!mesh->poly_valid(seed) || polyGroup.contains(seed))
                continue;

            const int32_t gid = nextGroup++;
            polyGroup[seed]   = gid;

            std::vector<int32_t> stack{seed};

            while (!stack.empty())
            {
                const int32_t p = stack.back();
                stack.pop_back();

                if (!mesh->poly_valid(p))
                    continue;

                const SysPolyVerts& pv = mesh->poly_verts(p);
                const int           n  = pv.size();

                for (int i = 0; i < n; ++i)
                {
                    const int32_t a = pv[i];
                    const int32_t b = pv[(i + 1) % n];

                    if (is_sel(a, b))
                        continue; // selected edge — boundary between groups

                    for (int32_t q : mesh->edge_polys(mesh->sort_edge({a, b})))
                    {
                        if (q == p || !editableSet.contains(q) || polyGroup.contains(q))
                            continue;
                        polyGroup[q] = gid;
                        stack.push_back(q);
                    }
                }
            }
        }

        // -------------------------------------------------------
        // 5) Accumulate miter inset positions per (group, origVert)
        //
        //    Each editable poly contributes one miter sample for every
        //    corner that touches a selected edge. Samples from different
        //    polys in the same group are averaged so corners shared by
        //    two polys in the same group land on the same point.
        // -------------------------------------------------------
        struct InsetKey
        {
            int32_t g{}, v{};
            bool    operator==(const InsetKey& o) const noexcept { return g == o.g && v == o.v; }
        };
        struct InsetKeyHash
        {
            std::size_t operator()(const InsetKey& k) const noexcept
            {
                return std::hash<int32_t>{}(k.g) ^
                       (std::hash<int32_t>{}(k.v) * 2654435761u);
            }
        };

        struct Accum
        {
            glm::vec3 sum{0.0f};
            int32_t   count{0};
        };

        std::unordered_map<InsetKey, Accum, InsetKeyHash> insetAccum;
        insetAccum.reserve(4096);

        for (int32_t p : editablePolys)
        {
            if (!mesh->poly_valid(p))
                continue;

            auto itg = polyGroup.find(p);
            if (itg == polyGroup.end())
                continue;

            const int32_t gid = itg->second;

            const SysPolyVerts& pv = mesh->poly_verts(p);
            const int           n  = pv.size();
            if (n < 3)
                continue;

            for (int i = 0; i < n; ++i)
            {
                const int32_t vPrev = pv[(i + n - 1) % n];
                const int32_t v     = pv[i];
                const int32_t vNext = pv[(i + 1) % n];

                const bool selIn  = is_sel(vPrev, v);
                const bool selOut = is_sel(v, vNext);

                if (!selIn && !selOut)
                    continue;

                const glm::vec3 pos = miter_pos(mesh, p, vPrev, v, vNext, selIn, selOut, w);

                InsetKey k{gid, v};
                auto&    a = insetAccum[k];
                a.sum += pos;
                a.count++;
            }
        }

        // -------------------------------------------------------
        // 6) Create inset vertices from averaged positions
        // -------------------------------------------------------
        std::unordered_map<InsetKey, int32_t, InsetKeyHash> insetVert;
        insetVert.reserve(insetAccum.size() * 2u);

        for (const auto& [k, a] : insetAccum)
        {
            if (a.count <= 0)
                continue;

            const int32_t nv = mesh->create_vert(a.sum / float(a.count));
            insetVert[k]     = nv;
        }

        if (insetVert.empty())
            return;

        auto inset_for = [&](int32_t gid, int32_t v) noexcept -> int32_t {
            auto it = insetVert.find({gid, v});
            return (it != insetVert.end()) ? it->second : -1;
        };

        // -------------------------------------------------------
        // 7) Build replacement polys for editable region
        //    Each corner that touches a selected edge swaps to its
        //    inset vertex; corners that don't are left unchanged.
        // -------------------------------------------------------
        struct NewPoly
        {
            SysPolyVerts verts{};
            uint32_t     mat{0};
        };

        std::vector<NewPoly> newEditPolys;
        newEditPolys.reserve(editablePolys.size());

        for (int32_t p : editablePolys)
        {
            if (!mesh->poly_valid(p))
                continue;

            const int32_t gid = polyGroup.count(p) ? polyGroup[p] : -1;
            if (gid < 0)
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(p);
            const int           n  = pv.size();
            if (n < 3)
                continue;

            SysPolyVerts out;
            out.reserve(n);

            for (int i = 0; i < n; ++i)
            {
                const int32_t vPrev = pv[(i + n - 1) % n];
                const int32_t v     = pv[i];
                const int32_t vNext = pv[(i + 1) % n];

                if (is_sel(vPrev, v) || is_sel(v, vNext))
                {
                    const int32_t vi = inset_for(gid, v);
                    out.push_back(vi >= 0 ? vi : v);
                }
                else
                {
                    out.push_back(v);
                }
            }

            if (out.size() >= 3)
                newEditPolys.push_back({out, mesh->poly_material(p)});
        }

        // -------------------------------------------------------
        // 8) Build band quads across each interior selected edge
        //    (edge shared by two editable polys in different groups)
        // -------------------------------------------------------
        std::vector<NewPoly>         bandQuads;
        std::unordered_set<uint64_t> bridged;
        bandQuads.reserve(selEdges.size());
        bridged.reserve(selEdges.size() * 2u);

        for (const EdgeSnap& snap : edgeSnaps)
        {
            // Collect the two editable polys on either side
            int32_t p = -1, q = -1;
            for (int32_t pid : snap.polys)
            {
                if (!editableSet.contains(pid))
                    continue;
                (p < 0 ? p : q) = pid;
                if (q >= 0)
                    break;
            }

            if (p < 0 || q < 0)
                continue;

            const int32_t gp = polyGroup.count(p) ? polyGroup[p] : -1;
            const int32_t gq = polyGroup.count(q) ? polyGroup[q] : -1;
            if (gp < 0 || gq < 0)
                continue;

            const int32_t a = snap.e.first;
            const int32_t b = snap.e.second;

            if (!bridged.insert(un::pack_undirected_i32(a, b)).second)
                continue;

            const int32_t a2p = inset_for(gp, a);
            const int32_t b2p = inset_for(gp, b);
            const int32_t a2q = inset_for(gq, a);
            const int32_t b2q = inset_for(gq, b);

            if (a2p < 0 || b2p < 0 || a2q < 0 || b2q < 0)
                continue;

            // Winding: build candidate quad then check against averaged
            // poly normals. Swap if necessary.
            SysPolyVerts quad;
            quad.reserve(4);
            quad.push_back(a2p);
            quad.push_back(b2p);
            quad.push_back(b2q);
            quad.push_back(a2q);

            {
                const glm::vec3  N  = un::safe_normalize(mesh->poly_normal(p) +
                                                       mesh->poly_normal(q));
                const glm::vec3& P0 = mesh->vert_position(quad[0]);
                const glm::vec3& P1 = mesh->vert_position(quad[1]);
                const glm::vec3& P2 = mesh->vert_position(quad[2]);
                const glm::vec3  Nb = un::safe_normalize(glm::cross(P1 - P0, P2 - P0));

                // If the normals are nearly antiparallel, use the original
                // edge direction projected against the poly normal instead.
                float alignment = glm::dot(Nb, N);
                if (std::abs(alignment) < 1e-4f)
                {
                    // Coplanar fallback: use the original edge as reference.
                    const glm::vec3 edgeDir = un::safe_normalize(
                        mesh->vert_position(b) - mesh->vert_position(a));
                    const glm::vec3 bandDir = un::safe_normalize(
                        mesh->vert_position(a2q) - mesh->vert_position(a2p));
                    alignment = glm::dot(glm::cross(edgeDir, bandDir), N);
                }

                if (alignment < 0.0f)
                    std::swap(quad[1], quad[3]);
            }

            bandQuads.push_back({quad, mesh->poly_material(p)});
        }

        // -------------------------------------------------------
        // 9) Apply: remove old editable polys, create new ones
        // -------------------------------------------------------
        for (int32_t p : editablePolys)
            if (mesh->poly_valid(p))
                mesh->remove_poly(p);

        for (const NewPoly& np : newEditPolys)
            if (np.verts.size() >= 3)
                mesh->create_poly(np.verts, np.mat);

        for (const NewPoly& np : bandQuads)
            if (np.verts.size() >= 3)
                mesh->create_poly(np.verts, np.mat);
    }

    // ----------------------------------------------------------
    //  bevelPolys — bevel the boundary edges of selected polys
    // ----------------------------------------------------------
    void bevelPolys(SysMesh* mesh, std::span<const int32_t> polys, float amount, bool group)
    {
        if (!mesh || polys.empty())
            return;

        const float w = std::abs(amount);
        if (un::is_zero(w))
            return;

        std::vector<std::vector<int32_t>> groups;

        if (group)
        {
            groups.emplace_back(polys.begin(), polys.end());
        }
        else
        {
            for (int32_t p : polys)
                groups.push_back({p});
        }

        for (const auto& grp : groups)
        {
            std::unordered_set<int32_t> grpSet(grp.begin(), grp.end());

            std::vector<IndexPair> boundary;
            boundary.reserve(grp.size() * 4u);

            for (int32_t p : grp)
            {
                if (!mesh->poly_valid(p))
                    continue;

                for (const IndexPair& e : mesh->poly_edges(p))
                {
                    // Boundary edge: only one adjacent poly is in the group.
                    int inGroup = 0;
                    for (int32_t q : mesh->edge_polys(e))
                        if (q >= 0 && grpSet.contains(q))
                            ++inGroup;

                    if (inGroup == 1)
                        boundary.push_back(e);
                }
            }

            if (!boundary.empty())
                bevelEdges(mesh, boundary, w);
        }
    }

    // ----------------------------------------------------------
    //  bevelVerts
    //
    //  For each selected vertex V:
    //   1. Create one new vertex per incident edge at distance w.
    //   2. Rebuild each incident poly replacing V with [newA, newB].
    //   3. Build a cap polygon from the new verts in winding order.
    // ----------------------------------------------------------
    void bevelVerts(SysMesh* mesh, std::span<const int32_t> verts, float width)
    {
        if (!mesh || verts.empty())
            return;

        const float w = std::abs(width);
        if (un::is_zero(w))
            return;

        // Polys rebuilt by a previous vert in the same call must not be
        // rebuilt again (they already have the correct new verts).
        std::unordered_set<int32_t> alreadyRebuilt;
        alreadyRebuilt.reserve(512);

        for (int32_t v : verts)
        {
            if (v < 0 || !mesh->vert_valid(v))
                continue;

            const glm::vec3 P = mesh->vert_position(v);

            // Gather incident polys that haven't been rebuilt yet.
            std::vector<int32_t> incPolys;
            incPolys.reserve(mesh->vert_polys(v).size());

            for (int32_t p : mesh->vert_polys(v))
            {
                if (p < 0 || !mesh->poly_valid(p))
                    continue;
                if (alreadyRebuilt.contains(p))
                    continue;

                // Verify v is actually in this poly's ring.
                bool found = false;
                for (int32_t vi : mesh->poly_verts(p))
                    if (vi == v)
                    {
                        found = true;
                        break;
                    }

                if (found)
                    incPolys.push_back(p);
            }

            if (incPolys.size() < 2)
                continue;

            // -----------------------------------------------
            // 1) Create one new vertex per distinct neighbor
            //    along the edge from V toward that neighbor.
            // -----------------------------------------------
            std::unordered_map<int32_t, int32_t> edgeVert; // neighbor -> new vert id
            edgeVert.reserve(incPolys.size() * 2u);

            auto ensure_edge_vert = [&](int32_t neighbor) {
                if (!mesh->vert_valid(neighbor) || edgeVert.contains(neighbor))
                    return;
                const glm::vec3 dir = un::safe_normalize(mesh->vert_position(neighbor) - P);
                edgeVert[neighbor]  = mesh->create_vert(P + dir * w);
            };

            // Also build the winding-order adjacency for cap construction.
            // nextOf[prevNeighbor] = nextNeighbor  (from poly winding around V)
            std::unordered_map<int32_t, int32_t> nextOf;
            nextOf.reserve(incPolys.size() * 2u);

            for (int32_t p : incPolys)
            {
                const SysPolyVerts& pv = mesh->poly_verts(p);
                const int           n  = pv.size();
                if (n < 3)
                    continue;

                int vi = -1;
                for (int i = 0; i < n; ++i)
                    if (pv[i] == v)
                    {
                        vi = i;
                        break;
                    }
                if (vi < 0)
                    continue;

                const int32_t vPrev = pv[(vi + n - 1) % n];
                const int32_t vNext = pv[(vi + 1) % n];

                ensure_edge_vert(vPrev);
                ensure_edge_vert(vNext);

                // Only record the first occurrence to keep a consistent
                // traversal direction around the fan.
                if (!nextOf.contains(vPrev))
                    nextOf[vPrev] = vNext;
            }

            if (edgeVert.size() < 3)
                continue;

            // -----------------------------------------------
            // 2) Detect open fan (boundary vertex)
            // -----------------------------------------------
            // In a closed manifold fan every neighbor in nextOf is also
            // a key in nextOf (the chain loops). In an open fan two
            // neighbors are endpoints — they appear as values but not keys,
            // or keys but not values.
            std::unordered_set<int32_t> nextOfKeys, nextOfVals;
            for (const auto& [k, val] : nextOf)
            {
                nextOfKeys.insert(k);
                nextOfVals.insert(val);
            }

            // Endpoint: a key whose value is NOT a key (end of chain)
            // or a value that is NOT a key (start of chain).
            int32_t chainStart = -1; // first neighbor of the open fan
            for (const auto& [k, val] : nextOf)
            {
                if (!nextOfKeys.contains(val))
                {
                    chainStart = k; // k's successor is an endpoint
                    break;
                }
            }
            // For a closed fan chainStart stays -1.

            // -----------------------------------------------
            // 3) Rebuild each incident poly: V → [newPrev, newNext]
            // -----------------------------------------------
            struct NewPoly
            {
                SysPolyVerts verts{};
                uint32_t     mat{0};
            };
            std::vector<NewPoly> rebuilt;
            rebuilt.reserve(incPolys.size());

            for (int32_t p : incPolys)
            {
                const SysPolyVerts& pv = mesh->poly_verts(p);
                const int           n  = pv.size();
                if (n < 3)
                    continue;

                int vi = -1;
                for (int i = 0; i < n; ++i)
                    if (pv[i] == v)
                    {
                        vi = i;
                        break;
                    }
                if (vi < 0)
                    continue;

                const int32_t vPrev = pv[(vi + n - 1) % n];
                const int32_t vNext = pv[(vi + 1) % n];

                auto itA = edgeVert.find(vPrev);
                auto itB = edgeVert.find(vNext);
                if (itA == edgeVert.end() || itB == edgeVert.end())
                    continue;

                // Replace v with [a2, b2] in the ring.
                SysPolyVerts out;
                out.reserve(n + 1);
                for (int k = 0; k < vi; ++k)
                    out.push_back(pv[k]);
                out.push_back(itA->second); // new prev
                out.push_back(itB->second); // new next
                for (int k = vi + 1; k < n; ++k)
                    out.push_back(pv[k]);

                // Remove consecutive duplicates (can arise at sharp corners).
                SysPolyVerts clean;
                clean.reserve(out.size());
                for (int32_t x : out)
                    if (clean.empty() || clean.back() != x)
                        clean.push_back(x);
                if (clean.size() >= 2 && clean.front() == clean.back())
                    clean.pop_back();

                if (clean.size() >= 3)
                    rebuilt.push_back({clean, mesh->poly_material(p)});
            }

            if (rebuilt.empty())
                continue;

            // -----------------------------------------------
            // 4) Build cap polygon in winding order
            // -----------------------------------------------
            SysPolyVerts cap;

            if (chainStart < 0)
            {
                // Closed fan: traverse the cycle.
                int32_t       cur      = nextOf.begin()->first;
                const int32_t start    = cur;
                const int     maxSteps = static_cast<int>(edgeVert.size()) + 4;

                for (int step = 0; step < maxSteps; ++step)
                {
                    auto it = nextOf.find(cur);
                    if (it == nextOf.end())
                        break;

                    const int32_t nxt = it->second;
                    auto          itV = edgeVert.find(nxt);
                    if (itV != edgeVert.end())
                        cap.push_back(itV->second);

                    cur = nxt;
                    if (cur == start)
                        break;
                }
            }
            else
            {
                // Open fan: traverse the chain from chainStart.
                // The cap for an open fan is a polygon along the boundary;
                // push the new vert for chainStart first, then follow chain.
                int32_t   cur      = chainStart;
                const int maxSteps = static_cast<int>(edgeVert.size()) + 4;

                {
                    auto itV = edgeVert.find(cur);
                    if (itV != edgeVert.end())
                        cap.push_back(itV->second);
                }

                for (int step = 0; step < maxSteps; ++step)
                {
                    auto it = nextOf.find(cur);
                    if (it == nextOf.end())
                        break;

                    const int32_t nxt = it->second;
                    auto          itV = edgeVert.find(nxt);
                    if (itV != edgeVert.end())
                        cap.push_back(itV->second);

                    cur = nxt;
                }

                // Open fans produce a valid cap only if they have >= 3 verts;
                // otherwise it's just a blade and no cap is needed.
                if (cap.size() < 3)
                    cap.clear();
            }

            // Verify cap winding against the poly normal of the first
            // incident poly and flip if necessary.
            if (cap.size() >= 3)
            {
                const glm::vec3 N  = mesh->poly_normal(incPolys.front());
                const glm::vec3 P0 = mesh->vert_position(cap[0]);
                const glm::vec3 P1 = mesh->vert_position(cap[1]);
                const glm::vec3 P2 = mesh->vert_position(cap[2]);
                const glm::vec3 Nc = glm::cross(P1 - P0, P2 - P0);

                // The cap should face AWAY from the surface (outward normal
                // relative to the fan). If the cap normal agrees with the
                // surface normal, reverse it.
                if (glm::dot(Nc, N) > 0.0f)
                    std::reverse(cap.begin(), cap.end());
            }

            // -----------------------------------------------
            // 5) Apply: remove old polys, add rebuilt + cap
            // -----------------------------------------------
            for (int32_t p : incPolys)
            {
                if (mesh->poly_valid(p))
                {
                    alreadyRebuilt.insert(p);
                    mesh->remove_poly(p);
                }
            }

            for (const NewPoly& np : rebuilt)
                if (np.verts.size() >= 3)
                    mesh->create_poly(np.verts, np.mat);

            if (cap.size() >= 3)
            {
                const uint32_t capMat = mesh->poly_material(incPolys.front());
                mesh->create_poly(cap, capMat);
            }

            // Clean up isolated original vertex (no polys left referencing it).
            if (mesh->vert_polys(v).empty())
                mesh->remove_vert(v);
        }
    }

} // namespace ops::sys
