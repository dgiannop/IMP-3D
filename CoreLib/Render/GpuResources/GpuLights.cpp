// ============================================================
// GpuLights.cpp  (WORLD-space LightsUBO: directional/point/spot)
// ============================================================
//
// Conventions after this change:
//   - All lights in GpuLightsUBO are expressed in WORLD space.
//     * Directional: lights[i].dir_range.xyz = forward (WORLD)
//     * Point:       lights[i].pos_type.xyz  = position (WORLD)
//     * Spot:        lights[i].pos_type.xyz  = position (WORLD)
//                  lights[i].dir_range.xyz  = forward (WORLD)
//   - lights[i].dir_range.w:
//     * Directional: angular radius (radians) for soft shadows (optional)
//     * Point/Spot:  range (world units), 0 = inverse-square only
//
// Shaders must now treat point/spot as world-space too.
// (Migrated SOLID + SHADED directional; this completes the UBO side.)
// ============================================================

#include "GpuLights.hpp"

#include <algorithm>
#include <cmath>

#include "CoreUtilities.hpp"
#include "Light.hpp"
#include "LightHandler.hpp"
#include "LightingSettings.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    // ------------------------------------------------------------------------
    // Headlight test switches
    // ------------------------------------------------------------------------
    // If true, use a fixed world-space sun direction (useful for debugging).
    constexpr bool kUseFixedSunForHeadlight = false;

    // If true, inject the headlight as a WORLD-SPACE spotlight at the camera
    // (a "flashlight" that always follows the view).
    constexpr bool kHeadlightAsFlashlightSpot = false;

    // Headlight bias (camera-locked directional):
    // Keep this subtle for SOLID.
    constexpr bool  kUseHeadlightBias   = true;
    constexpr float kHeadlightSideBias  = +0.10f; // negative = left
    constexpr float kHeadlightUpBias    = 0.00f;  // positive = up (try -0.05f if top still darkens)
    constexpr float kHeadlightBiasScale = 1.0f;   // global scaler for quick tuning

    // Flashlight tuning (only used when kHeadlightAsFlashlightSpot == true).
    constexpr float kHeadlightRange    = 80.0f;
    constexpr float kHeadlightInnerRad = 10.0f * 3.14159265f / 180.0f;
    constexpr float kHeadlightOuterRad = 18.0f * 3.14159265f / 180.0f;

    // Directional softness (only used when injecting as directional).
    constexpr float kHeadlightSoftnessRadians = 0.05f; // ~3 degrees

    static glm::vec3 clamp01(const glm::vec3& c) noexcept
    {
        return glm::vec3(std::clamp(c.x, 0.0f, 1.0f),
                         std::clamp(c.y, 0.0f, 1.0f),
                         std::clamp(c.z, 0.0f, 1.0f));
    }

    static uint32_t lightCount(const GpuLightsUBO& ubo) noexcept
    {
        return ubo.info.x;
    }

    static void setLightCount(GpuLightsUBO& ubo, uint32_t v) noexcept
    {
        ubo.info.x = v;
    }

    static void pushLight(GpuLightsUBO& out, const GpuLight& l) noexcept
    {
        uint32_t count = lightCount(out);
        if (count >= kMaxGpuLights)
            return;

        out.lights[count] = l;
        setLightCount(out, count + 1u);
    }

    static GpuLight makeDirectionalWorld(const glm::vec3& dirWorld, const glm::vec3& color, float intensity) noexcept
    {
        GpuLight gl = {};
        gl.pos_type = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(GpuLightType::Directional));

        const glm::vec3 fwdWorld = un::safe_normalize(dirWorld, glm::vec3(0, 0, -1));

        // xyz = forward (WORLD space), w = softness (angular radius in radians)
        gl.dir_range = glm::vec4(fwdWorld, kHeadlightSoftnessRadians);

        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(0.0f);

        return gl;
    }

    static GpuLight makePointWorld(const glm::vec3& posWorld,
                                   const glm::vec3& color,
                                   float            intensity,
                                   float            range) noexcept
    {
        GpuLight gl        = {};
        gl.pos_type        = glm::vec4(posWorld, static_cast<float>(GpuLightType::Point));
        gl.dir_range       = glm::vec4(0.0f, 0.0f, 0.0f, std::max(0.0f, range)); // w = range
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(0.0f);
        return gl;
    }

    static GpuLight makeSpotWorld(const glm::vec3& posWorld,
                                  const glm::vec3& dirWorld,
                                  const glm::vec3& color,
                                  float            intensity,
                                  float            range,
                                  float            innerConeRad,
                                  float            outerConeRad) noexcept
    {
        const float inner = std::max(0.0f, innerConeRad);
        const float outer = std::max(inner, outerConeRad);

        const float innerCos = std::cos(inner);
        const float outerCos = std::cos(outer);

        GpuLight gl        = {};
        gl.pos_type        = glm::vec4(posWorld, static_cast<float>(GpuLightType::Spot));
        gl.dir_range       = glm::vec4(un::safe_normalize(dirWorld, glm::vec3(0, 0, -1)), std::max(0.0f, range)); // w = range
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(innerCos, outerCos, 0.0f, 0.0f);
        return gl;
    }

    static glm::vec3 viewportForwardWorld(const Viewport& vp) noexcept
    {
        // Assumes vp.view() is WORLD->VIEW
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 fwd  = glm::vec3(invV * glm::vec4(0, 0, -1, 0));
        return un::safe_normalize(fwd, glm::vec3(0, 0, -1));
    }

    static glm::vec3 viewportRightWorld(const Viewport& vp) noexcept
    {
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 rgt  = glm::vec3(invV * glm::vec4(1, 0, 0, 0));
        return un::safe_normalize(rgt, glm::vec3(1, 0, 0));
    }

    static glm::vec3 viewportUpWorld(const Viewport& vp) noexcept
    {
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 up   = glm::vec3(invV * glm::vec4(0, 1, 0, 0));
        return un::safe_normalize(up, glm::vec3(0, 1, 0));
    }

    static glm::vec3 viewportPosWorld(const Viewport& vp) noexcept
    {
        // Camera origin in VIEW space is (0,0,0); transform to world.
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 p    = glm::vec3(invV * glm::vec4(0, 0, 0, 1));
        return p;
    }

    static LightingSettings::ModePolicy policyForDrawMode(const LightingSettings& s, DrawMode mode) noexcept
    {
        switch (mode)
        {
            case DrawMode::SOLID:
                return s.solidPolicy;
            case DrawMode::SHADED:
                return s.shadedPolicy;
            case DrawMode::RAY_TRACE:
                return s.rtPolicy;
            default:
                return LightingSettings::ModePolicy::Both;
        }
    }

    static bool allowHeadlight(const LightingSettings& s, DrawMode mode) noexcept
    {
        if (!s.useHeadlight)
            return false;

        const auto p = policyForDrawMode(s, mode);
        return (p == LightingSettings::ModePolicy::HeadlightOnly || p == LightingSettings::ModePolicy::Both);
    }

    static bool allowSceneLights(const LightingSettings& s, DrawMode mode) noexcept
    {
        if (!s.useSceneLights)
            return false;

        const auto p = policyForDrawMode(s, mode);
        return (p == LightingSettings::ModePolicy::SceneOnly || p == LightingSettings::ModePolicy::Both);
    }

} // namespace

void buildGpuLightsUBO(const LightingSettings&  settings,
                       const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept
{
    out         = {};
    out.info    = glm::uvec4(0u);
    out.ambient = glm::vec4(0.f);

    const DrawMode dm = vp.drawMode();

    // ------------------------------------------------------------
    // 1) Modeling headlight (optional)
    // ------------------------------------------------------------
    // Render-time "modeling light" expressed in WORLD space. This is not a SceneLight.
    if (allowHeadlight(settings, dm) && headlight.enabled && headlight.intensity > 0.0f)
    {
        if (kHeadlightAsFlashlightSpot)
        {
            // "Flashlight" headlight:
            // - Positioned at the camera origin in WORLD space.
            // - Points forward along the camera forward vector in WORLD space.
            const glm::vec3 posWorld       = viewportPosWorld(vp);
            const glm::vec3 dirTowardWorld = viewportForwardWorld(vp);

            pushLight(out,
                      makeSpotWorld(posWorld,
                                    dirTowardWorld,
                                    headlight.color,
                                    headlight.intensity,
                                    kHeadlightRange,
                                    kHeadlightInnerRad,
                                    kHeadlightOuterRad));
        }
        else
        {
            // Directional camera-locked headlight (WORLD):
            glm::vec3 dirWorld = {};

            if (kUseFixedSunForHeadlight)
            {
                dirWorld = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f));
            }
            else
            {
                const glm::vec3 forward = viewportForwardWorld(vp);

                if (kUseHeadlightBias && (std::abs(kHeadlightSideBias) > 0.0f || std::abs(kHeadlightUpBias) > 0.0f))
                {
                    const glm::vec3 right = viewportRightWorld(vp);
                    const glm::vec3 up    = viewportUpWorld(vp);

                    const float side = kHeadlightSideBias * kHeadlightBiasScale;
                    const float upb  = kHeadlightUpBias * kHeadlightBiasScale;

                    dirWorld = un::safe_normalize(forward + (side * right) + (upb * up), forward);
                }
                else
                {
                    dirWorld = forward;
                }
            }

            pushLight(out, makeDirectionalWorld(dirWorld, headlight.color, headlight.intensity));
        }
    }

    // ------------------------------------------------------------
    // 2) Scene lights (optional) (WORLD)
    // ------------------------------------------------------------
    uint32_t sceneLightCount = 0u;
    float    maxSceneLight   = 0.0f;

    if (allowSceneLights(settings, dm) && scene)
    {
        const LightHandler* lh = scene->lightHandler();
        if (lh)
        {
            const auto ids = lh->allLights();
            for (LightId id : ids)
            {
                const Light* l = lh->light(id);
                if (!l || !l->enabled)
                    continue;

                const float cmax = std::max(l->color.x, std::max(l->color.y, l->color.z));
                maxSceneLight    = std::max(maxSceneLight, std::max(0.0f, l->intensity) * std::max(0.0f, cmax));
                ++sceneLightCount;

                switch (l->type)
                {
                    case LightType::Directional:
                        pushLight(out, makeDirectionalWorld(l->direction, l->color, l->intensity));
                        break;

                    case LightType::Point:
                        pushLight(out, makePointWorld(l->position, l->color, l->intensity, l->range));
                        break;

                    case LightType::Spot:
                        pushLight(out,
                                  makeSpotWorld(l->position,
                                                l->direction,
                                                l->color,
                                                l->intensity,
                                                l->range,
                                                l->spotInnerConeRad,
                                                l->spotOuterConeRad));
                        break;
                }

                if (out.info.x >= kMaxGpuLights)
                    break;
            }
        }
    }

    // ------------------------------------------------------------
    // 3) Ambient.rgb and Ambient.a = EXPOSURE
    // ------------------------------------------------------------
    const float fill = std::max(0.0f, settings.ambientFill);
    out.ambient      = glm::vec4(fill, fill, fill, 0.0f);

    float exposure = 1.0f;

    if (sceneLightCount > 0u && maxSceneLight > 0.0f)
    {
        constexpr float kKey = 0.18f;
        exposure             = kKey / maxSceneLight;
        exposure             = std::clamp(exposure, 1e-6f, 2.0f);
    }

    exposure *= std::max(0.0f, settings.exposure);

    out.ambient.a = exposure;
}
