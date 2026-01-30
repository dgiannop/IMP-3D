#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

class Viewport;

/// OverlayHandler
///
/// Collects per-frame overlay shapes made of points, lines and polygons.
/// Used by tools (BoxSizer, etc.) to:
///  - build gizmos (via begin_overlay / add_point / add_line / end_overlay)
///  - pick a handle under the mouse (pick)
///  - let the Renderer draw them (lines()).
///
/// v1.1 change: overlay handles are int32_t (no strings).
///  - handle == -1 means "none".
class OverlayHandler
{
public:
    static constexpr int32_t kNoHandle = -1;

    struct Point
    {
        glm::vec3 pos   = {};
        float     size  = 1.0f;
        glm::vec4 color = {};
    };

    struct Line
    {
        glm::vec3 p1        = {};
        glm::vec3 p2        = {};
        float     thickness = 1.0f;
        glm::vec4 color     = {};
    };

    struct Polygon
    {
        std::vector<glm::vec3> verts;
        glm::vec4              color = {};
    };

    /// Begin specifying a new overlay shape with a given handle.
    /// Typically tools use stable indices for handles (0..N-1).
    void begin_overlay(int32_t handle);

    /// Add primitives to the current overlay.
    void add_point(const glm::vec3& point, float size, const glm::vec4& color);

    void add_line(const glm::vec3& p1, const glm::vec3& p2, float thickness, const glm::vec4& color);

    void add_polygon(const std::vector<glm::vec3>& points, const glm::vec4& color);

    /// Set the “axis” for the current overlay.
    /// Used by legacy gizmo picking fallback.
    void set_axis(const glm::vec3& axis);

    /// Finish the current overlay.
    void end_overlay();

    /// Clear all overlays for the next frame.
    void clear();

    /// Picking: returns the overlay handle under the mouse, or kNoHandle (-1).
    /// Uses projected 2D distances in screen space (same coords passed in from CoreEvent).
    int32_t pick(Viewport* vp, float mouse_x, float mouse_y);

    /// Access all overlay lines flattened across all shapes (for Renderer::drawOverlays).
    const std::vector<Point>&   points() const;
    const std::vector<Line>&    lines() const;
    const std::vector<Polygon>& polygons() const;

private:
    struct Shape
    {
        int32_t              handle = kNoHandle;
        glm::vec3            axis{0.0f};
        std::vector<Point>   points;
        std::vector<Line>    lines;
        std::vector<Polygon> polys;
    };

    std::vector<Shape> m_shapes;
    int32_t            m_currentShape = kNoHandle;

    // Scratch buffers for flattened primitives (for rendering)
    mutable std::vector<Line>    m_flatLines;
    mutable std::vector<Point>   m_flatPoints;
    mutable std::vector<Polygon> m_flatPolys;

    int32_t m_lastPicked = kNoHandle;
};
