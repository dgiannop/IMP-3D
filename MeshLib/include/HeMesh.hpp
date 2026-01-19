#pragma once

/**
 * @brief Half-edge / loop-style mesh for DCC-style tools.
 *
 * PIMPL-wrapped: all topology (verts, edges, polys, loops) and
 * connectivity live in HeMeshData inside the .cpp. The public API
 * exposes only stable IDs and high-level adjacency queries.
 */

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// Forward declaration of your container type.
template<typename T>
class HoleList;

namespace un
{
    template<class T, int32_t N>
    class small_list;
} // namespace un

/**
 * @brief Half-edge / loop-style mesh for DCC-style tools.
 *
 * PIMPL-wrapped: all topology (verts, edges, polys, loops) and
 * connectivity live in HeMeshData inside the .cpp. The public API
 * exposes only stable IDs and high-level adjacency queries.
 */
class HeMesh
{
public:
    // ---------------------------------------------------------
    // Public ID types
    // ---------------------------------------------------------
    using VertId = int32_t;
    using EdgeId = int32_t;
    using PolyId = int32_t;
    using LoopId = int32_t;

    static inline constexpr VertId kInvalidVert = -1;
    static inline constexpr EdgeId kInvalidEdge = -1;
    static inline constexpr PolyId kInvalidPoly = -1;
    static inline constexpr LoopId kInvalidLoop = -1;

    // Small fixed-capacity adjacency containers
    using PolyVerts = un::small_list<VertId, 8>;
    using PolyEdges = un::small_list<EdgeId, 8>;
    using PolyLoops = un::small_list<LoopId, 8>;
    using EdgePolys = un::small_list<PolyId, 8>;
    using VertPolys = un::small_list<PolyId, 8>;
    using VertEdges = un::small_list<EdgeId, 8>;
    using VertVerts = un::small_list<VertId, 8>;

public:
    // ---------------------------------------------------------
    // Construction / lifetime
    // ---------------------------------------------------------
    HeMesh();
    ~HeMesh();

    HeMesh(const HeMesh&)            = delete;
    HeMesh& operator=(const HeMesh&) = delete;

    HeMesh(HeMesh&& other) noexcept;
    HeMesh& operator=(HeMesh&& other) noexcept;

    /// Reset to empty mesh.
    void clear() noexcept;

    /// Reserve approximate capacities for performance.
    void reserve(int32_t vertCount,
                 int32_t edgeCount,
                 int32_t polyCount,
                 int32_t loopCount);

    // ---------------------------------------------------------
    // Creation / Removal (PUBLIC)
    // ---------------------------------------------------------
    VertId createVert(const glm::vec3& pos);
    PolyId createPoly(const std::vector<VertId>& verts, uint32_t material = 0);

    /// Always returns an existing or freshly created edge
    EdgeId ensureEdge(VertId v0, VertId v1);

    void removeVert(VertId v);
    void removePoly(PolyId p);
    void removeEdge(EdgeId e);

    // Removes edges that are no longer referenced by any loop.
    // IMPORTANT: Call this BEFORE removeIsolatedVerts(), since vertices
    // may still be referenced by edges even after all loops are gone.
    void removeUnusedEdges();

    // Removes vertices that are no longer referenced by any loop.
    // Must be called AFTER removeUnusedEdges().
    void removeIsolatedVerts();

    [[nodiscard]] EdgeId findEdge(VertId v0, VertId v1) const noexcept;

    // ---------------------------------------------------------
    // Counts
    // ---------------------------------------------------------
    [[nodiscard]] int32_t vertCount() const noexcept;
    [[nodiscard]] int32_t edgeCount() const noexcept;
    [[nodiscard]] int32_t polyCount() const noexcept;
    [[nodiscard]] int32_t loopCount() const noexcept;

    // ---------------------------------------------------------
    // Validity
    // ---------------------------------------------------------
    [[nodiscard]] bool vertValid(VertId v) const noexcept;
    [[nodiscard]] bool edgeValid(EdgeId e) const noexcept;
    [[nodiscard]] bool polyValid(PolyId p) const noexcept;
    [[nodiscard]] bool loopValid(LoopId l) const noexcept;

    // ---------------------------------------------------------
    // Geometry access
    // ---------------------------------------------------------
    [[nodiscard]] glm::vec3 position(VertId v) const noexcept;

    void setPosition(VertId v, const glm::vec3& pos) noexcept;

    [[nodiscard]] uint32_t polyMaterial(PolyId p) const noexcept;

    void setPolyMaterial(PolyId p, uint32_t materialId) noexcept;

    [[nodiscard]] glm::vec3 polyNormal(PolyId pid) const noexcept;

    // ---------------------------------------------------------
    // Per-loop (face-varying) attributes
    // ---------------------------------------------------------
    [[nodiscard]] bool loopHasUV(LoopId l) const noexcept;

    [[nodiscard]] glm::vec2 loopUV(LoopId l) const noexcept;

    void setLoopUV(LoopId l, const glm::vec2& uv) noexcept;
    void clearLoopUV(LoopId l) noexcept;

    [[nodiscard]] bool loopHasNormal(LoopId l) const noexcept;

    [[nodiscard]] glm::vec3 loopNormal(LoopId l) const noexcept;

    void setLoopNormal(LoopId l, const glm::vec3& n) noexcept;
    void clearLoopNormal(LoopId l) noexcept;

    // ---------------------------------------------------------
    // High-level adjacency (tool-facing, read-only)
    // ---------------------------------------------------------

    /// Ordered vertices of polygon p (like SysMesh::poly_verts).
    [[nodiscard]] PolyVerts polyVerts(PolyId p) const;

    /// Ordered edges of polygon p.
    [[nodiscard]] PolyEdges polyEdges(PolyId p) const;

    /// Ordered loops of polygon p (face-corners).
    [[nodiscard]] PolyLoops polyLoops(PolyId p) const;

    /// Polygons incident to edge e (radial fan).
    [[nodiscard]] EdgePolys edgePolys(EdgeId e) const;

    /// Polygons incident to vertex v (vertex fan).
    [[nodiscard]] VertPolys vertPolys(VertId v) const;

    /// Edges incident to vertex v.
    [[nodiscard]] VertEdges vertEdges(VertId v) const;

    /// Neighbor vertices directly connected to v by edges.
    [[nodiscard]] VertVerts vertVerts(VertId v) const;

    /// Endpoints of edge e (v0, v1).
    [[nodiscard]] std::pair<VertId, VertId> edgeVerts(EdgeId e) const noexcept;

    /// All valid vertex IDs (stable indices).
    [[nodiscard]] std::span<const VertId> allVerts() const noexcept;

    /// All valid edge IDs (stable indices).
    [[nodiscard]] std::span<const EdgeId> allEdges() const noexcept;

    /// All valid polygon IDs (stable indices).
    [[nodiscard]] std::span<const PolyId> allPolys() const noexcept;

    // ---------------------------------------------------------
    // Low-level loop access (half-edge style)
    // ---------------------------------------------------------

    /// First loop of polygon p (face-corner handle).
    [[nodiscard]] LoopId polyFirstLoopId(PolyId p) const noexcept;

    /// Any loop on edge e (one of the radial ring).
    [[nodiscard]] LoopId edgeAnyLoopId(EdgeId e) const noexcept;

    /// Loop attribute accessors (topology navigation).
    [[nodiscard]] VertId loopVertId(LoopId l) const noexcept;
    [[nodiscard]] EdgeId loopEdgeId(LoopId l) const noexcept;
    [[nodiscard]] PolyId loopPolyId(LoopId l) const noexcept;

    /// Next/prev along polygon boundary.
    [[nodiscard]] LoopId loopNextId(LoopId l) const noexcept;
    [[nodiscard]] LoopId loopPrevId(LoopId l) const noexcept;

    /// Next/prev around the undirected edge (radial fan).
    [[nodiscard]] LoopId loopRadialNextId(LoopId l) const noexcept;
    [[nodiscard]] LoopId loopRadialPrevId(LoopId l) const noexcept;

    /// Twin loop across the same undirected edge on a different polygon (or kInvalidLoop on boundary).
    [[nodiscard]] LoopId loopTwinId(LoopId l) const noexcept;

    /// Find the directed loop (a -> b) inside polygon p, or kInvalidLoop.
    [[nodiscard]] LoopId findLoop(PolyId p, VertId a, VertId b) const noexcept;

    /// Insert vNew between (a,b) on polygon p's ring (either direction must exist).
    /// This updates polygon ring links and edge radial rings.
    /// Returns false if p does not contain edge (a,b) or inputs are invalid.
    bool insertVertOnPolyEdge(PolyId p, VertId a, VertId b, VertId vNew);

    // ---------------------------------------------------------
    // Simple modeling operations
    // ---------------------------------------------------------

    /// Split edge e at parameter t (0..1), inserting a new vertex
    /// and updating all incident polygons. Returns the new VertId
    /// or kInvalidVert on failure.
    VertId splitEdge(EdgeId e, float t = 0.5f);

    /// Collapse edge e to a single vertex.
    VertId collapseEdge(EdgeId e);

    /// Dissolve edge e if it has exactly two incident polygons,
    /// merging them into a single n-gon. No vertex removal.
    void dissolveEdge(EdgeId e);

    /// Weld two vertices: move all loops from 'kill' to 'keep'
    /// and mark 'kill' as removed. Does not yet merge duplicate
    /// edges; that can be added later.
    VertId weldVerts(VertId keep, VertId kill);

    // ---------------------------------------------------------
    // Edge loops / rings (for selection tools)
    // ---------------------------------------------------------

    /// Edge loop traversal starting from 'start'.
    /// Currently assumes mostly manifold quad-ish topology.
    [[nodiscard]] std::vector<EdgeId> edgeLoop(EdgeId start) const;

    /// Edge ring traversal (stub for now, returns just start).
    [[nodiscard]] std::vector<EdgeId> edgeRing(EdgeId start) const;

    void debugPrint() const;
    bool validate() const;
    /// Validate edgeLoop / edgeRing for all edges.
    bool validateLoopsAndRings(int32_t maxDebugEdges = 16) const;

    /// Replace the entire vertex ring of polygon p with a new ordered list.
    /// Automatically rebuilds loops and updates all adjacency/radial fans.
    /// Material is preserved.
    void setPolyVerts(PolyId p, const std::vector<VertId>& verts);

private:
    struct HeMeshData;
    std::unique_ptr<HeMeshData> m_data;
};
