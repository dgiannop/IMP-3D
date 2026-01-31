//=============================================================================
// OverlayHandler.cpp
//=============================================================================
#include "OverlayHandler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Viewport.hpp"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static float clamp01(float v) noexcept
{
    return std::max(0.0f, std::min(1.0f, v));
}

static float dist2(const glm::vec2& a, const glm::vec2& b) noexcept
{
    const glm::vec2 d = a - b;
    return d.x * d.x + d.y * d.y;
}

static float distPointToSegment2(const glm::vec2& p,
                                 const glm::vec2& a,
                                 const glm::vec2& b,
                                 float&           outT) noexcept
{
    const glm::vec2 ab  = b - a;
    const float     ab2 = ab.x * ab.x + ab.y * ab.y;

    if (ab2 <= 1e-12f)
    {
        outT = 0.0f;
        return dist2(p, a);
    }

    const glm::vec2 ap = p - a;
    const float     t  = clamp01((ap.x * ab.x + ap.y * ab.y) / ab2);

    outT = t;

    const glm::vec2 q = a + ab * t;
    return dist2(p, q);
}

static bool pointInPolygon2D(const glm::vec2&              p,
                             const std::vector<glm::vec2>& poly) noexcept
{
    // Ray casting / even-odd rule.
    bool inside = false;

    const size_t n = poly.size();
    if (n < 3)
        return false;

    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const glm::vec2& a = poly[j];
        const glm::vec2& b = poly[i];

        const bool condY = ((a.y > p.y) != (b.y > p.y));
        if (!condY)
            continue;

        const float dy = (b.y - a.y);
        const float t  = (dy != 0.0f) ? ((p.y - a.y) / dy) : 0.0f;
        const float xI = a.x + (b.x - a.x) * t;

        if (p.x < xI)
            inside = !inside;
    }

    return inside;
}

// -----------------------------------------------------------------------------
// OverlayHandler
// -----------------------------------------------------------------------------

void OverlayHandler::clear() noexcept
{
    m_overlays.clear();
    m_buildIndex = -1;
}

OverlayHandler::Overlay* OverlayHandler::current() noexcept
{
    if (m_buildIndex < 0)
        return nullptr;

    const int32_t idx = m_buildIndex;
    if (idx >= (int32_t)m_overlays.size())
        return nullptr;

    return &m_overlays[idx];
}

void OverlayHandler::begin_overlay(int32_t id)
{
    // End previous implicitly if caller forgot.
    // This keeps the builder robust during rapid refactors.
    m_buildIndex = (int32_t)m_overlays.size();

    Overlay o = {};
    o.id      = id;
    o.axis    = glm::vec3(0.0f);

    m_overlays.push_back(std::move(o));
}

void OverlayHandler::end_overlay()
{
    m_buildIndex = -1;
}

void OverlayHandler::set_axis(const glm::vec3& axis)
{
    Overlay* o = current();
    if (!o)
        return;

    o->axis = axis;
}

void OverlayHandler::set_axis(const glm::ivec3& axis)
{
    set_axis(glm::vec3((float)axis.x, (float)axis.y, (float)axis.z));
}

void OverlayHandler::add_line(const glm::vec3& a,
                              const glm::vec3& b,
                              float            thicknessPx,
                              const glm::vec4& color)
{
    Overlay* o = current();
    if (!o)
        return;

    Line l      = {};
    l.a         = a;
    l.b         = b;
    l.thickness = thicknessPx;
    l.color     = color;

    o->lines.push_back(l);
}

void OverlayHandler::add_point(const glm::vec3& p,
                               float            sizePx,
                               const glm::vec4& color)
{
    Overlay* o = current();
    if (!o)
        return;

    Point pt = {};
    pt.p     = p;
    pt.size  = sizePx;
    pt.color = color;

    o->points.push_back(pt);
}

void OverlayHandler::add_polygon(const std::vector<glm::vec3>& verts,
                                 const glm::vec4&              color)
{
    Overlay* o = current();
    if (!o)
        return;

    if (verts.size() < 3)
        return;

    Polygon poly = {};
    poly.verts   = verts;
    poly.color   = color;

    o->polygons.push_back(std::move(poly));
}

int32_t OverlayHandler::pick(const Viewport* vp, float x, float y) const
{
    if (!vp)
        return -1;

    const glm::vec2 mouse(x, y);

    int32_t bestId    = -1;
    float   bestDist2 = std::numeric_limits<float>::infinity();
    float   bestDepth = std::numeric_limits<float>::infinity();

    // We scan all overlays and all their shapes.
    // We prefer smallest screen distance; tie-breaker is nearest depth (smaller z).
    for (const Overlay& o : m_overlays)
    {
        // -------------------------------------------------------------
        // 1) Points
        // -------------------------------------------------------------
        for (const Point& p : o.points)
        {
            const glm::vec3 sp3 = vp->project(p.p);
            const glm::vec2 sp(sp3.x, sp3.y);

            const float d2 = dist2(mouse, sp);

            const float r  = std::max(m_pickPointRadiusPx, p.size * 0.75f);
            const float r2 = r * r;

            if (d2 <= r2)
            {
                const float depth = sp3.z;

                if (d2 < bestDist2 || (d2 == bestDist2 && depth < bestDepth))
                {
                    bestDist2 = d2;
                    bestDepth = depth;
                    bestId    = o.id;
                }
            }
        }

        // -------------------------------------------------------------
        // 2) Lines
        // -------------------------------------------------------------
        for (const Line& l : o.lines)
        {
            const glm::vec3 a3 = vp->project(l.a);
            const glm::vec3 b3 = vp->project(l.b);

            const glm::vec2 a(a3.x, a3.y);
            const glm::vec2 b(b3.x, b3.y);

            float       t  = 0.0f;
            const float d2 = distPointToSegment2(mouse, a, b, t);

            // Use max of global pick radius and line thickness.
            const float r  = std::max(m_pickLineRadiusPx, l.thickness * 0.75f);
            const float r2 = r * r;

            if (d2 <= r2)
            {
                // Approx depth at closest point.
                const float depth = a3.z + (b3.z - a3.z) * t;

                if (d2 < bestDist2 || (d2 == bestDist2 && depth < bestDepth))
                {
                    bestDist2 = d2;
                    bestDepth = depth;
                    bestId    = o.id;
                }
            }
        }

        // -------------------------------------------------------------
        // 3) Polygons (interior hit)
        // -------------------------------------------------------------
        for (const Polygon& poly : o.polygons)
        {
            const size_t n = poly.verts.size();
            if (n < 3)
                continue;

            std::vector<glm::vec2> sp;
            sp.reserve(n);

            float depthAcc = 0.0f;
            int   depthN   = 0;

            for (size_t i = 0; i < n; ++i)
            {
                const glm::vec3 p3 = vp->project(poly.verts[i]);
                sp.push_back(glm::vec2(p3.x, p3.y));
                depthAcc += p3.z;
                depthN++;
            }

            if (sp.size() < 3 || depthN == 0)
                continue;

            if (!pointInPolygon2D(mouse, sp))
                continue;

            // Inside polygon = treat as perfect hit distance (0).
            const float d2    = 0.0f;
            const float depth = depthAcc / float(depthN);

            if (d2 < bestDist2 || (d2 == bestDist2 && depth < bestDepth))
            {
                bestDist2 = d2;
                bestDepth = depth;
                bestId    = o.id;
            }
        }
    }

    return bestId;
}
