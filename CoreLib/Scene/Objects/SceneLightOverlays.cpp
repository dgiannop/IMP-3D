//=============================================================================
// SceneLightOverlays.cpp
//=============================================================================
#include "SceneLightOverlays.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "Light.hpp"
#include "LightHandler.hpp"
#include "OverlayHandler.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    constexpr int32_t kOverlayLightBase = 100000;

    [[nodiscard]] inline int32_t overlayIdFromLightId(LightId id) noexcept
    {
        return kOverlayLightBase + static_cast<int32_t>(id);
    }

    [[nodiscard]] inline glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
    {
        const float d2 = glm::dot(v, v);
        if (d2 < 1e-12f)
            return fallback;
        return v * (1.0f / std::sqrt(d2));
    }

    inline void buildBillboardCircle(Viewport*        vp,
                                     OverlayHandler&  ov,
                                     const glm::vec3& center,
                                     float            radiusWorld,
                                     const glm::vec4& color,
                                     int              segs = 32)
    {
        const glm::vec3 right = vp->rightDirection();
        const glm::vec3 up    = vp->upDirection();

        segs = std::max(3, segs);

        std::vector<glm::vec3> pts;
        pts.resize((std::size_t)segs);

        for (int i = 0; i < segs; ++i)
        {
            const float t = (float(i) / float(segs)) * (2.0f * kPi);
            const float c = std::cos(t);
            const float s = std::sin(t);

            pts[(std::size_t)i] = center + right * (c * radiusWorld) + up * (s * radiusWorld);
        }

        ov.add_polygon(pts, color);
    }

    inline void buildArrow(OverlayHandler&  ov,
                           const glm::vec3& origin,
                           const glm::vec3& dir,
                           float            length,
                           float            headSize,
                           float            thicknessPx,
                           const glm::vec4& color,
                           const glm::vec3& upHint)
    {
        const glm::vec3 d  = safeNormalize(dir, glm::vec3(0, 0, -1));
        const glm::vec3 p0 = origin;
        const glm::vec3 p1 = origin + d * length;

        ov.add_line(p0, p1, thicknessPx, color);

        glm::vec3 side = glm::cross(d, upHint);
        side           = safeNormalize(side, glm::vec3(1, 0, 0));

        const glm::vec3 back = p1 - d * headSize;
        const glm::vec3 l    = back + side * (0.6f * headSize);
        const glm::vec3 r    = back - side * (0.6f * headSize);

        ov.add_line(p1, l, thicknessPx, color);
        ov.add_line(p1, r, thicknessPx, color);
    }

    inline void buildSpotConeWire(OverlayHandler&  ov,
                                  const glm::vec3& pos,
                                  const glm::vec3& dir,
                                  float            length,
                                  float            outerConeRad,
                                  float            thicknessPx,
                                  const glm::vec4& color,
                                  const glm::vec3& upHint)
    {
        const glm::vec3 d = safeNormalize(dir, glm::vec3(0, 0, -1));

        glm::vec3 right = glm::cross(d, upHint);
        right           = safeNormalize(right, glm::vec3(1, 0, 0));

        glm::vec3 up = glm::cross(right, d);
        up           = safeNormalize(up, glm::vec3(0, 1, 0));

        const glm::vec3 baseC = pos + d * length;

        outerConeRad  = std::max(0.0f, outerConeRad);
        const float r = std::tan(outerConeRad) * length;

        constexpr int kSegs = 24;

        glm::vec3 prev = baseC + right * r;
        for (int i = 1; i <= kSegs; ++i)
        {
            const float t = (float(i) / float(kSegs)) * (2.0f * kPi);
            const float c = std::cos(t);
            const float s = std::sin(t);

            const glm::vec3 cur = baseC + right * (c * r) + up * (s * r);
            ov.add_line(prev, cur, thicknessPx, color);
            prev = cur;
        }

        for (int i = 0; i < 6; ++i)
        {
            const float t = (float(i) / 6.0f) * (2.0f * kPi);
            const float c = std::cos(t);
            const float s = std::sin(t);

            const glm::vec3 rim = baseC + right * (c * r) + up * (s * r);
            ov.add_line(pos, rim, thicknessPx, color);
        }
    }
} // namespace

namespace scene_overlays
{
    void appendLights(Viewport* vp, Scene* scene, OverlayHandler& outOverlays)
    {
        if (!vp || !scene)
            return;

        const LightHandler* lh  = scene->lightHandler();
        const auto          ids = lh->allLights();

        for (LightId id : ids)
        {
            const Light* L = lh->light(id);
            if (!L || !L->enabled)
                continue;

            const glm::vec3 pos = L->position;
            const glm::vec3 dir = safeNormalize(L->direction, glm::vec3(0, 0, -1));

            const float px = std::max(0.0001f, vp->pixelScale(pos));

            const float iconR   = std::max(0.0001f, px * 8.0f);
            const float thickPx = 3.0f;

            const glm::vec4 color = glm::vec4(1, 1, 0, 0.9f);

            outOverlays.begin_overlay(overlayIdFromLightId(id));

            switch (L->type)
            {
                case LightType::Point: {
                    buildBillboardCircle(vp, outOverlays, pos, iconR, color, 32);

                    outOverlays.add_line(pos - vp->rightDirection() * (iconR * 1.2f),
                                         pos + vp->rightDirection() * (iconR * 1.2f),
                                         thickPx,
                                         color);

                    outOverlays.add_line(pos - vp->upDirection() * (iconR * 1.2f),
                                         pos + vp->upDirection() * (iconR * 1.2f),
                                         thickPx,
                                         color);
                    break;
                }

                case LightType::Directional: {
                    const float len  = std::max(0.01f, px * 65.0f);
                    const float head = std::max(0.01f, px * 18.0f);

                    buildArrow(outOverlays, pos, dir, len, head, thickPx, color, vp->upDirection());
                    buildBillboardCircle(vp, outOverlays, pos, iconR, color, 24);
                    break;
                }

                case LightType::Spot: {
                    const float len = std::max(0.05f, px * 85.0f);

                    buildSpotConeWire(outOverlays,
                                      pos,
                                      dir,
                                      len,
                                      L->spotOuterConeRad,
                                      thickPx,
                                      color,
                                      vp->upDirection());

                    buildBillboardCircle(vp, outOverlays, pos, iconR, color, 24);

                    if (L->spotInnerConeRad > 0.0f)
                    {
                        const glm::vec4 innerCol = glm::vec4(color.r, color.g, color.b, 0.45f);
                        buildSpotConeWire(outOverlays,
                                          pos,
                                          dir,
                                          len,
                                          L->spotInnerConeRad,
                                          thickPx,
                                          innerCol,
                                          vp->upDirection());
                    }
                    break;
                }

                default: {
                    buildBillboardCircle(vp, outOverlays, pos, iconR, color, 24);
                    break;
                }
            }

            outOverlays.set_axis(dir);
            outOverlays.end_overlay();
        }
    }
} // namespace scene_overlays
