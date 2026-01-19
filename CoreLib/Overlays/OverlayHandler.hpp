#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

class Viewport;

/// OverlayHandler
///
/// Collects per-frame overlay shapes made of points, lines and polygons.
/// Used by tools (BoxSizer, etc.) to:
///  - build gizmos (via begin_overlay / add_point / add_line / end_overlay)
///  - pick a handle under the mouse (pick)
///  - let the Renderer draw them (lines()).
class OverlayHandler
{
public:
    struct Point
    {
        glm::vec3 pos;
        float     size;
        glm::vec4 color;
    };

    struct Line
    {
        glm::vec3 p1;
        glm::vec3 p2;
        float     thickness;
        glm::vec4 color;
    };

    struct Polygon
    {
        std::vector<glm::vec3> verts;
        glm::vec4              color;
    };

    /// Begin specifying a new overlay shape with a given name.
    /// Typically BoxSizer uses std::to_string(handleIndex) as the name.
    void begin_overlay(const std::string& name);

    /// Add primitives to the current overlay.
    void add_point(const glm::vec3& point, float size, const glm::vec4& color);

    void add_line(const glm::vec3& p1, const glm::vec3& p2, float thickness, const glm::vec4& color);

    void add_polygon(const std::vector<glm::vec3>& points, const glm::vec4& color);

    /// Set the “axis” for the current overlay.
    /// Used to prefer handles that are not collinear with the picking ray.
    void set_axis(const glm::vec3& axis);

    /// Finish the current overlay.
    void end_overlay();

    /// Clear all overlays for the next frame.
    void clear();

    /// Picking: returns the name of the overlay under the mouse, or an empty string.
    /// Uses projected 2D distances in screen space (same coords passed in from CoreEvent).
    const std::string& pick(Viewport* vp, float mouse_x, float mouse_y);

    /// Access all overlay lines flattened across all shapes (for Renderer::drawOverlays).
    const std::vector<Point>&   points() const;
    const std::vector<Line>&    lines() const;
    const std::vector<Polygon>& polygons() const;

private:
    struct Shape
    {
        std::string          name;
        glm::vec3            axis{0.0f};
        std::vector<Point>   points;
        std::vector<Line>    lines;
        std::vector<Polygon> polys;
    };

    std::vector<Shape> m_shapes;
    int32_t            m_currentShape = -1;

    // Scratch buffers for flattened primitives (for rendering)
    mutable std::vector<Line>    m_flatLines;
    mutable std::vector<Point>   m_flatPoints;
    mutable std::vector<Polygon> m_flatPolys;

    std::string m_emptyString;
};
