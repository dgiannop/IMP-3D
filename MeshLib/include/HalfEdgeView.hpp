#pragma once

#include <SmallList.hpp>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class SysMesh;

/**
 * @brief Derived loop (half-edge) connectivity view for SysMesh (read-only, tool-facing).
 *
 * Built from SysMesh::poly_verts(pid) winding. Provides loopNext/loopTwin links and
 * convenience adjacency queries for traversal in tools (bevel, extrude, loops/rings).
 *
 * Not authoritative mesh data. Not stored in undo/redo. Tools still modify SysMesh.
 */
class HalfEdgeView
{
public:
    using VertId = int32_t;
    using PolyId = int32_t;
    using LoopId = int32_t;

    static inline constexpr VertId kInvalidVert = -1;
    static inline constexpr PolyId kInvalidPoly = -1;
    static inline constexpr LoopId kInvalidLoop = -1;

    /// Small fixed-capacity adjacency containers.
    using PolyVerts = un::small_list<VertId, 8>;
    using PolyLoops = un::small_list<LoopId, 8>;
    using PolyPolys = un::small_list<PolyId, 8>;

    /// Undirected edge key (no stored EdgeId needed).
    using EdgeKey = std::pair<VertId, VertId>;

public:
    HalfEdgeView();
    ~HalfEdgeView();

    HalfEdgeView(const HalfEdgeView&)                = delete;
    HalfEdgeView& operator=(const HalfEdgeView&)     = delete;
    HalfEdgeView(HalfEdgeView&&) noexcept            = delete;
    HalfEdgeView& operator=(HalfEdgeView&&) noexcept = delete;

    // ---------------------------------------------------------
    // Build / lifetime
    // ---------------------------------------------------------

    /// Clear all derived connectivity.
    void clear() noexcept;

    /// Build the view from the given SysMesh (read-only snapshot).
    void build(const SysMesh* mesh);

    /// True if no loops were built.
    [[nodiscard]] bool empty() const noexcept;

    /// Total number of loops (half-edges).
    [[nodiscard]] int32_t loopCount() const noexcept;

    // ---------------------------------------------------------
    // Loop core accessors
    // ---------------------------------------------------------

    /// True if l is in range and refers to a stored loop.
    [[nodiscard]] bool loopValid(LoopId l) const noexcept;

    /// Origin vertex of directed loop l (a -> b).
    [[nodiscard]] VertId loopFrom(LoopId l) const noexcept;

    /// Destination vertex of directed loop l (a -> b).
    [[nodiscard]] VertId loopTo(LoopId l) const noexcept;

    /// Polygon that owns loop l.
    [[nodiscard]] PolyId loopPoly(LoopId l) const noexcept;

    /// Next loop in the same polygon ring (same winding).
    [[nodiscard]] LoopId loopNext(LoopId l) const noexcept;

    /// Previous loop in the same polygon ring (same winding).
    [[nodiscard]] LoopId loopPrev(LoopId l) const noexcept;

    /// Twin loop across the same undirected edge, opposite direction (invalid if boundary/non-manifold).
    [[nodiscard]] LoopId loopTwin(LoopId l) const noexcept;

    /// Corner index of loop l in its polygon (index into SysMesh::poly_verts and map_poly_verts for that poly).
    [[nodiscard]] int32_t loopCorner(LoopId l) const noexcept;

    /// Normalized (undirected) edge key for loop l.
    [[nodiscard]] EdgeKey loopEdgeKey(LoopId l) const noexcept;

    // ---------------------------------------------------------
    // Polygon entry + ordered polygon traversal
    // ---------------------------------------------------------

    /// True if polygon p has an entry loop and is represented in the view.
    [[nodiscard]] bool polyValid(PolyId p) const noexcept;

    /// First loop of polygon p (or invalid).
    [[nodiscard]] LoopId polyFirstLoop(PolyId p) const noexcept;

    /// Ordered loops of polygon p (same order as SysMesh::poly_verts).
    [[nodiscard]] PolyLoops polyLoops(PolyId p) const;

    /// Ordered vertex ring of polygon p (same order as SysMesh::poly_verts).
    [[nodiscard]] PolyVerts polyVerts(PolyId p) const;

    /// Neighbor polys across each loop of p (invalid if boundary/non-manifold on that side).
    [[nodiscard]] PolyPolys polyNeighborPolys(PolyId p) const;

    // ---------------------------------------------------------
    // Tool helpers (minimal set for bevel/extrude/loops)
    // ---------------------------------------------------------

    /// Find a directed loop (a -> b). Returns invalid if not found.
    [[nodiscard]] LoopId findLoop(VertId a, VertId b) const noexcept;

    /// Find any loop representing the undirected edge (a,b). Returns either direction if present.
    [[nodiscard]] LoopId findEdge(VertId a, VertId b) const noexcept;

    /// Collect incident polys for an undirected edge (0=unused, 1=boundary, 2=manifold, >2=non-manifold).
    [[nodiscard]] std::vector<PolyId> edgePolys(VertId a, VertId b) const;

    /// Basic edge-loop traversal starting from an undirected edge (a,b) (quad-manifold oriented).
    [[nodiscard]] std::vector<EdgeKey> edgeLoop(VertId a, VertId b) const;

    /// Basic edge-ring traversal starting from an undirected edge (a,b) (quad-manifold oriented).
    [[nodiscard]] std::vector<EdgeKey> edgeRing(VertId a, VertId b) const;

private:
    // Per-loop (half-edge) arrays
    std::vector<VertId>  m_loopFrom;
    std::vector<VertId>  m_loopTo;
    std::vector<PolyId>  m_loopPoly;
    std::vector<LoopId>  m_loopNext;
    std::vector<LoopId>  m_loopPrev;
    std::vector<LoopId>  m_loopTwin;
    std::vector<int32_t> m_loopCorner;

    // Per-poly entry
    std::vector<LoopId> m_polyFirstLoop;

    // Directed lookup for findLoop(a,b).
    // Keep the heavy containers out of the header.
    struct Lookup;
    std::unique_ptr<Lookup> m_lookup;
};
