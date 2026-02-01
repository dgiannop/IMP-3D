//=============================================================================
// LightOverlaySystem.cpp
//=============================================================================
#include "LightOverlaySystem.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "LightHandler.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    void buildBillboardCircle(Viewport*        vp,
                              OverlayHandler&  ov,
                              const glm::vec3& center,
                              float            radiusWorld,
                              const glm::vec4& color,
                              int              segs)
    {
        const glm::vec3 right = vp->rightDirection();
        const glm::vec3 up    = vp->upDirection();

        std::vector<glm::vec3> pts;
        pts.resize(std::max(3, segs));

        for (int i = 0; i < (int)pts.size(); ++i)
        {
            const float t = (float(i) / float(pts.size())) * (2.0f * kPi);
            const float c = std::cos(t);
            const float s = std::sin(t);
            pts[i]        = center + right * (c * radiusWorld) + up * (s * radiusWorld);
        }

        ov.add_polygon(pts, color);
    }

    void buildArrow(OverlayHandler&  ov,
                    const glm::vec3& origin,
                    const glm::vec3& dir,
                    float            lengthWorld,
                    float            headWorld,
                    float            thicknessPx,
                    const glm::vec4& color,
                    const glm::vec3& upHint)
    {
        const glm::vec3 d  = dir;
        const glm::vec3 p0 = origin;
        const glm::vec3 p1 = origin + d * lengthWorld;

        ov.add_line(p0, p1, thicknessPx, color);

        glm::vec3   side = glm::cross(d, upHint);
        const float s2   = glm::dot(side, side);
        if (s2 < 1e-12f)
            side = glm::vec3(1, 0, 0);
        else
            side *= 1.0f / std::sqrt(s2);

        const glm::vec3 back = p1 - d * headWorld;
        const glm::vec3 l    = back + side * (0.6f * headWorld);
        const glm::vec3 r    = back - side * (0.6f * headWorld);

        ov.add_line(p1, l, thicknessPx, color);
        ov.add_line(p1, r, thicknessPx, color);
    }

    void buildConeWire(OverlayHandler&  ov,
                       const glm::vec3& pos,
                       const glm::vec3& dir,
                       float            lengthWorld,
                       float            outerConeRad,
                       float            thicknessPx,
                       const glm::vec4& color,
                       const glm::vec3& upHint)
    {
        const glm::vec3 d = dir;

        glm::vec3   right = glm::cross(d, upHint);
        const float r2    = glm::dot(right, right);
        if (r2 < 1e-12f)
            right = glm::vec3(1, 0, 0);
        else
            right *= 1.0f / std::sqrt(r2);

        glm::vec3   up = glm::cross(right, d);
        const float u2 = glm::dot(up, up);
        if (u2 < 1e-12f)
            up = glm::vec3(0, 1, 0);
        else
            up *= 1.0f / std::sqrt(u2);

        const glm::vec3 baseC = pos + d * lengthWorld;
        const float     rr    = std::tan(std::max(0.0f, outerConeRad)) * lengthWorld;

        constexpr int kSegs = 24;

        glm::vec3 prev = baseC + right * rr;
        for (int i = 1; i <= kSegs; ++i)
        {
            const float t = (float(i) / float(kSegs)) * (2.0f * kPi);
            const float c = std::cos(t);
            const float s = std::sin(t);

            const glm::vec3 cur = baseC + right * (c * rr) + up * (s * rr);
            ov.add_line(prev, cur, thicknessPx, color);
            prev = cur;
        }

        // A few spokes for readability
        for (int i = 0; i < 6; ++i)
        {
            const float t = (float(i) / 6.0f) * (2.0f * kPi);
            const float c = std::cos(t);
            const float s = std::sin(t);

            const glm::vec3 rim = baseC + right * (c * rr) + up * (s * rr);
            ov.add_line(pos, rim, thicknessPx, color);
        }
    }
} // namespace

glm::vec3 LightOverlaySystem::safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
{
    const float d2 = glm::dot(v, v);
    if (d2 < 1e-12f)
        return fallback;
    return v * (1.0f / std::sqrt(d2));
}

void LightOverlaySystem::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    m_overlays.clear();

    const LightHandler*        lh  = scene->lightHandler(); // adjust if accessor differs
    const std::vector<LightId> ids = lh->allLights();

    for (LightId id : ids)
    {
        const Light* l = lh->light(id);
        if (!l || !l->enabled)
            continue;

        const glm::vec3 pos = l->position;
        const glm::vec3 dir = safeNormalize(l->direction, glm::vec3(0, 0, -1));

        const float px = std::max(0.0001f, vp->pixelScale(pos));

        // Color: you can later tint by selection, enabled, etc.
        const glm::vec4 color = glm::vec4(1, 1, 0, 0.9f);

        m_overlays.begin_overlay(overlayIdFromLightId(id));

        switch (l->type)
        {
            case LightType::Point:
                buildPoint(vp, *l, px, color);
                break;
            case LightType::Directional:
                buildDirectional(vp, *l, px, color);
                break;
            case LightType::Spot:
                buildSpot(vp, *l, px, color);
                break;
            default:
                buildPoint(vp, *l, px, color);
                break;
        }

        m_overlays.set_axis(dir);
        m_overlays.end_overlay();
    }
}

LightId LightOverlaySystem::pick(Viewport* vp, float mx, float my) const noexcept
{
    if (!vp)
        return kInvalidLightId;

    const int32_t hit = m_overlays.pick(vp, mx, my);
    if (!overlayIdIsLight(hit))
        return kInvalidLightId;

    return lightIdFromOverlayId(hit);
}

void LightOverlaySystem::buildPoint(Viewport* vp, const Light& l, float px, const glm::vec4& color)
{
    const float iconR = std::max(0.0001f, px * 8.0f);
    const float thick = 3.0f;

    buildBillboardCircle(vp, m_overlays, l.position, iconR, color, 32);

    // Crosshair
    const glm::vec3 r = vp->rightDirection();
    const glm::vec3 u = vp->upDirection();

    m_overlays.add_line(l.position - r * (iconR * 1.2f), l.position + r * (iconR * 1.2f), thick, color);
    m_overlays.add_line(l.position - u * (iconR * 1.2f), l.position + u * (iconR * 1.2f), thick, color);
}

void LightOverlaySystem::buildDirectional(Viewport* vp, const Light& l, float px, const glm::vec4& color)
{
    const glm::vec3 dir = safeNormalize(l.direction, glm::vec3(0, 0, -1));

    const float thick = 3.0f;
    const float len   = std::max(0.02f, px * 70.0f);
    const float head  = std::max(0.01f, px * 18.0f);
    const float diskR = std::max(0.0001f, px * 7.0f);

    buildArrow(m_overlays, l.position, dir, len, head, thick, color, vp->upDirection());
    buildBillboardCircle(vp, m_overlays, l.position, diskR, color, 24);
}

void LightOverlaySystem::buildSpot(Viewport* vp, const Light& l, float px, const glm::vec4& color)
{
    const glm::vec3 dir = safeNormalize(l.direction, glm::vec3(0, 0, -1));

    const float thick = 3.0f;
    const float len   = std::max(0.05f, px * 90.0f);
    const float diskR = std::max(0.0001f, px * 7.0f);

    buildConeWire(m_overlays, l.position, dir, len, l.spotOuterConeRad, thick, color, vp->upDirection());
    buildBillboardCircle(vp, m_overlays, l.position, diskR, color, 24);

    // Optional: inner cone in a lighter alpha
    if (l.spotInnerConeRad > 0.0f)
    {
        const glm::vec4 innerCol = glm::vec4(color.r, color.g, color.b, 0.45f);
        buildConeWire(m_overlays, l.position, dir, len, l.spotInnerConeRad, thick, innerCol, vp->upDirection());
    }
}
