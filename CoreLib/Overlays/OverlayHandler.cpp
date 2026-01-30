#include "OverlayHandler.hpp"

#include <cfloat>
#include <cmath>
#include <glm/gtx/compatibility.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/vector_query.hpp>

#include "CoreUtilities.hpp"
#include "Viewport.hpp"

void OverlayHandler::begin_overlay(int32_t handle)
{
    Shape s;
    s.handle = handle;
    s.axis   = glm::vec3(0.0f);

    m_shapes.push_back(std::move(s));
    m_currentShape = static_cast<int32_t>(m_shapes.size()) - 1;
}

void OverlayHandler::end_overlay()
{
    m_currentShape = kNoHandle;
}

void OverlayHandler::add_point(const glm::vec3& point, float size, const glm::vec4& color)
{
    if (m_currentShape < 0 || m_currentShape >= static_cast<int32_t>(m_shapes.size()))
        return;

    Point p;
    p.pos   = point;
    p.size  = size;
    p.color = color;

    m_shapes[m_currentShape].points.push_back(p);
}

void OverlayHandler::add_line(const glm::vec3& p1,
                              const glm::vec3& p2,
                              float            thickness,
                              const glm::vec4& color)
{
    if (m_currentShape < 0 || m_currentShape >= static_cast<int32_t>(m_shapes.size()))
        return;

    Line L;
    L.p1        = p1;
    L.p2        = p2;
    L.thickness = thickness;
    L.color     = color;

    m_shapes[m_currentShape].lines.push_back(L);
}

void OverlayHandler::add_polygon(const std::vector<glm::vec3>& points,
                                 const glm::vec4&              color)
{
    if (m_currentShape < 0 || m_currentShape >= static_cast<int32_t>(m_shapes.size()))
        return;

    Polygon poly;
    poly.verts = points;
    poly.color = color;

    m_shapes[m_currentShape].polys.push_back(std::move(poly));
}

void OverlayHandler::set_axis(const glm::vec3& axis)
{
    if (m_currentShape < 0 || m_currentShape >= static_cast<int32_t>(m_shapes.size()))
        return;

    m_shapes[m_currentShape].axis = axis;
}

void OverlayHandler::clear()
{
    m_shapes.clear();
    m_currentShape = kNoHandle;
    m_flatLines.clear();
    m_flatPoints.clear();
    m_flatPolys.clear();
    m_lastPicked = kNoHandle;
}

// -----------------------------------------------------------------------------
// Flatten accessors for rendering
// -----------------------------------------------------------------------------

const std::vector<OverlayHandler::Point>& OverlayHandler::points() const
{
    m_flatPoints.clear();

    for (const Shape& s : m_shapes)
        m_flatPoints.insert(m_flatPoints.end(), s.points.begin(), s.points.end());

    return m_flatPoints;
}

const std::vector<OverlayHandler::Line>& OverlayHandler::lines() const
{
    m_flatLines.clear();

    for (const Shape& s : m_shapes)
    {
        // 1) existing explicit lines
        m_flatLines.insert(m_flatLines.end(), s.lines.begin(), s.lines.end());

        // 2) polygon outlines -> line loops
        for (const Polygon& poly : s.polys)
        {
            const auto&  v = poly.verts;
            const size_t n = v.size();
            if (n < 2)
                continue;

            for (size_t i = 0; i < n; ++i)
            {
                const glm::vec3& p1 = v[i];
                const glm::vec3& p2 = v[(i + 1) % n];

                Line L;
                L.p1        = p1;
                L.p2        = p2;
                L.thickness = 1.0f;       // ignore for now; renderer doesn’t use it yet
                L.color     = poly.color; // same color as polygon

                m_flatLines.push_back(L);
            }
        }
    }

    return m_flatLines;
}

const std::vector<OverlayHandler::Polygon>& OverlayHandler::polygons() const
{
    m_flatPolys.clear();

    for (const Shape& s : m_shapes)
        m_flatPolys.insert(m_flatPolys.end(), s.polys.begin(), s.polys.end());

    return m_flatPolys;
}

// -----------------------------------------------------------------------------
// Picking (unchanged logic; returns handle instead of string)
// -----------------------------------------------------------------------------

int32_t OverlayHandler::pick(Viewport* vp, float mouse_x, float mouse_y)
{
    if (!vp || m_shapes.empty())
    {
        m_lastPicked = kNoHandle;
        return m_lastPicked;
    }

    const glm::vec2 mousePos(mouse_x, mouse_y);
    const un::ray   ray = vp->ray(mouse_x, mouse_y);

    float bestDist     = FLT_MAX;
    float bestDepth    = FLT_MAX;
    int   bestShapeIdx = -1;

    int colinearShapeIdx = -1;

    constexpr float pointPickRadius   = 10.0f;
    constexpr float linePickRadius    = 10.0f;
    constexpr float axisColinearEps   = 0.1f;
    constexpr float axisNonZeroTarget = 1.0f;

    auto depth_valid = [](float z) noexcept {
        return std::isfinite(z) && z > -1e6f;
    };

    for (int i = 0; i < static_cast<int>(m_shapes.size()); ++i)
    {
        const Shape& shape = m_shapes[i];

        // --- test points ---
        for (const Point& pt : shape.points)
        {
            glm::vec3 sp = vp->project(pt.pos);

            if (!depth_valid(sp.z))
                continue;

            const float dist = glm::distance(mousePos, glm::vec2(sp));
            if (dist <= pointPickRadius)
            {
                if (dist < bestDist || (dist == bestDist && sp.z < bestDepth))
                {
                    bestDist     = dist;
                    bestDepth    = sp.z;
                    bestShapeIdx = i;
                }
            }
        }

        // --- test lines ---
        for (const Line& L : shape.lines)
        {
            glm::vec3 s1 = vp->project(L.p1);
            glm::vec3 s2 = vp->project(L.p2);

            if (!depth_valid(s1.z) && !depth_valid(s2.z))
                continue;

            if (s1 == s2)
                continue;

            const glm::vec2 a(s1);
            const glm::vec2 b(s2);

            float t = un::closest_point_on_line(mousePos, a, b);
            if (t < 0.0f || t > 1.0f)
                continue;

            glm::vec2 closest = glm::mix(a, b, t);
            float     dist    = glm::distance(mousePos, closest);

            if (dist <= linePickRadius)
            {
                float depth = glm::mix(s1.z, s2.z, t);
                if (!depth_valid(depth))
                    continue;

                if (dist < bestDist || (dist == bestDist && depth < bestDepth))
                {
                    bestDist     = dist;
                    bestDepth    = depth;
                    bestShapeIdx = i;
                }
            }
        }

        // Colinear fallback only if nothing else hit (legacy)
        if (un::equal(glm::compAdd(glm::abs(shape.axis)), axisNonZeroTarget) &&
            glm::areCollinear(shape.axis, ray.dir, axisColinearEps))
        {
            colinearShapeIdx = i;
        }
    }

    if (bestShapeIdx >= 0 && bestShapeIdx < static_cast<int>(m_shapes.size()))
    {
        m_lastPicked = m_shapes[bestShapeIdx].handle;
        return m_lastPicked;
    }

    if (colinearShapeIdx >= 0 && colinearShapeIdx < static_cast<int>(m_shapes.size()))
    {
        m_lastPicked = m_shapes[colinearShapeIdx].handle;
        return m_lastPicked;
    }

    m_lastPicked = kNoHandle;
    return m_lastPicked;
}
