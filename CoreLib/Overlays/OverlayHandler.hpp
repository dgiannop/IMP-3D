#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

class Viewport;

/**
 * @file OverlayHandler.hpp
 * @brief Lightweight overlay geometry builder + picking support.
 *
 * The tool layer builds overlay primitives each frame (lines, points, polygons)
 * grouped into "overlays" identified by an int32 handle id.
 *
 * The renderer consumes the generated overlays to draw gizmos/handles.
 *
 * Picking:
 *  - pick() returns the overlay id of the best hit, or -1 if none.
 *  - points and lines are distance-tested in screen space.
 *  - polygons support interior hit-testing (screen-space point-in-poly).
 *
 * Notes:
 *  - The overlay axis is a hint for tool logic / render coloring / constraints.
 *  - Polygons are primarily used for center disks/rings/filled shapes; treat interior as hittable.
 */
class OverlayHandler
{
public:
    struct Line
    {
        glm::vec3 a         = glm::vec3(0.0f);
        glm::vec3 b         = glm::vec3(0.0f);
        float     thickness = 1.0f; // in pixels (renderer may treat as hint)
        glm::vec4 color     = glm::vec4(1.0f);
    };

    struct Point
    {
        glm::vec3 p     = glm::vec3(0.0f);
        float     size  = 6.0f; // in pixels (renderer may treat as hint)
        glm::vec4 color = glm::vec4(1.0f);
    };

    struct Polygon
    {
        std::vector<glm::vec3> verts       = {};
        glm::vec4              color       = glm::vec4{1.0f};
        bool                   filled      = false; // render filled tris
        float                  thicknessPx = 2.5f;  // outline thickness (stroke pass)
    };

    struct Overlay
    {
        int32_t id = -1;

        // Optional axis hint. Tools can use this to infer constraints.
        glm::vec3 axis = glm::vec3(0.0f);

        std::vector<Line>    lines;
        std::vector<Point>   points;
        std::vector<Polygon> polygons;
    };

public:
    OverlayHandler()  = default;
    ~OverlayHandler() = default;

    OverlayHandler(const OverlayHandler&)            = default;
    OverlayHandler& operator=(const OverlayHandler&) = default;

    OverlayHandler(OverlayHandler&&) noexcept            = default;
    OverlayHandler& operator=(OverlayHandler&&) noexcept = default;

public:
    /**
     * @brief Clears all overlays.
     */
    void clear() noexcept;

    /**
     * @brief Begins building a new overlay with the given id.
     *
     * You must call end_overlay() after emitting primitives.
     */
    void begin_overlay(int32_t id);

    /**
     * @brief Ends the current overlay.
     */
    void end_overlay();

    /**
     * @brief Sets axis hint for the current overlay (world-space direction).
     */
    void set_axis(const glm::vec3& axis);

    /**
     * @brief Sets axis hint for the current overlay from integer axis.
     */
    void set_axis(const glm::ivec3& axis);

    /**
     * @brief Adds a line segment (world space).
     */
    void add_line(const glm::vec3& a,
                  const glm::vec3& b,
                  float            thicknessPx,
                  const glm::vec4& color);

    /**
     * @brief Adds a point (world space).
     */
    void add_point(const glm::vec3& p,
                   float            sizePx,
                   const glm::vec4& color);

    /**
     * @brief Adds a polygon (world space). Used for disks/rings/filled shapes.
     *
     * Picking treats the polygon interior as hittable.
     */
    void add_polygon(const std::vector<glm::vec3>& verts,
                     const glm::vec4&              color);

    /**
     * @brief Adds a polygon (world space). Used for filled shapes.
     *
     * Picking treats the polygon interior as hittable.
     */
    void add_polygon(const std::vector<glm::vec3>& verts,
                     const glm::vec4&              color,
                     bool                          filled,
                     float                         thicknessPx = 2.5f);

    /**
     * @brief Adds a filled circle as a polygon fan approximation.
     *
     * The circle is generated in a plane whose normal is the current overlay axis hint.
     * If the axis hint is degenerate, +Z is used.
     *
     * @param center      World-space center.
     * @param radius      World-space radius.
     * @param color       Fill color.
     * @param thicknessPx Outline thickness in pixels (stroke pass).
     * @param segments    Tessellation segments (>= 3).
     */
    void add_filled_circle(const glm::vec3& center,
                           float            radius,
                           const glm::vec4& color,
                           float            thicknessPx = 2.5f,
                           int              segments    = 32);

    /**
     * @brief Picks the overlay id under the mouse in screen space.
     *
     * @param vp Viewport used for project().
     * @param x  Mouse x in pixels (top-left origin).
     * @param y  Mouse y in pixels (top-left origin).
     * @return overlay id, or -1 if no hit.
     */
    [[nodiscard]] int32_t pick(const Viewport* vp, float x, float y) const;

    /**
     * @brief Returns overlays for rendering.
     */
    [[nodiscard]] const std::vector<Overlay>& overlays() const noexcept
    {
        return m_overlays;
    }

private:
    Overlay* current() noexcept;

private:
    std::vector<Overlay> m_overlays;

    // Build state
    int32_t m_buildIndex = -1;

    // Pick tuning (screen space)
    float m_pickPointRadiusPx = 12.0f;
    float m_pickLineRadiusPx  = 10.0f;
};
