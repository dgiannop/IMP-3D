//============================================================
// GpuLights.cpp  (FULL REPLACEMENT)
//============================================================
// ============================================================
// GpuLights.cpp  (WORLD-space LightsUBO: directional/point/spot)
// ============================================================
//
// Conventions:
//   - All lights in GpuLightsUBO are expressed in WORLD space.
//     * Directional: lights[i].dir_range.xyz = forward (WORLD)
//     * Point:       lights[i].pos_type.xyz  = position (WORLD)
//     * Spot:        lights[i].pos_type.xyz  = position (WORLD)
//                   lights[i].dir_range.xyz  = forward (WORLD)
//   - lights[i].dir_range.w:
//     * Directional: angular radius (radians) for soft shadows (optional; 0 = hard)
//     * Point/Spot:  range (world units), 0 = inverse-square only
//
// RT-only soft shadows for point/spot:
//   - lights[i].spot_params.z stores angular radius (radians) for soft shadows.
//     (Directional softness stays in dir_range.w.)
//
// IMPORTANT:
//   - out.ambient.rgb = ambient fill color (already scaled by ambientFill)
//   - out.ambient.a   = exposure scalar used by shaders
//
// Production/editor behavior (this file):
//   - NO auto-exposure derived from max light intensity.
//   - out.ambient.a = kBaseExposureScalar * settings.exposure
// ============================================================

#include "GpuLights.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    constexpr bool kUseFixedSunForHeadlight   = false;
    constexpr bool kHeadlightAsFlashlightSpot = false;

    // Headlight bias (camera-locked directional)
    constexpr bool  kUseHeadlightBias   = true;
    constexpr float kHeadlightSideBias  = +0.10f;
    constexpr float kHeadlightUpBias    = 0.00f;
    constexpr float kHeadlightBiasScale = 1.0f;

    // Flashlight tuning
    constexpr float kHeadlightRange    = 80.0f;
    constexpr float kHeadlightInnerRad = 10.0f * 3.14159265f / 180.0f;
    constexpr float kHeadlightOuterRad = 18.0f * 3.14159265f / 180.0f;

    // Directional softness for headlight (angular radius in radians)
    constexpr float kHeadlightSoftnessRadians = 0.05f; // ~3 degrees

    // Scene-spot cone safety clamps (radians)
    constexpr float kMinSpotOuterRad = 0.5f * 3.14159265f / 180.0f;  // 0.5°
    constexpr float kMaxSpotOuterRad = 89.0f * 3.14159265f / 180.0f; // 89°

    // ------------------------------------------------------------
    // RT soft shadows: point/spot angular radii (radians)
    // ------------------------------------------------------------
    // 0 = hard. These are "area light" approximations. Tune as needed.
    constexpr float kScenePointSoftnessRadians = 0.01f; // ~0.57°
    constexpr float kSceneSpotSoftnessRadians  = 0.01f; // ~0.57°

    // ------------------------------------------------------------
    // Exposure calibration
    // ------------------------------------------------------------
    // Baseline exposure scalar; settings.exposure is a UI multiplier.
    constexpr float kBaseExposureScalar = 0.02f;

    // Safety clamp for the final exposure scalar.
    constexpr float kMinFinalExposure = 0.0f;
    constexpr float kMaxFinalExposure = 8.0f;

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
        const uint32_t count = lightCount(out);
        if (count >= kMaxGpuLights)
            return;

        out.lights[count] = l;
        setLightCount(out, count + 1u);
    }

    // Positive-only clamp that FALLS BACK to a default on 0/negative/NaN.
    static float clampPos(float v, float def = 1.0f) noexcept
    {
        if (!std::isfinite(v))
            return def;

        if (!(v > 0.0f))
            return def;

        return v;
    }

    // Non-negative clamp that ALLOWS 0.
    static float clampNonNeg(float v, float def = 1.0f) noexcept
    {
        if (!std::isfinite(v))
            return def;

        return std::max(0.0f, v);
    }

    static void scaleSpotCones(float& innerRad, float& outerRad, float mul) noexcept
    {
        mul = std::clamp(mul, 0.05f, 10.0f);

        float inV  = std::max(0.0f, innerRad) * mul;
        float outV = std::max(0.0f, outerRad) * mul;

        outV = std::clamp(outV, kMinSpotOuterRad, kMaxSpotOuterRad);
        inV  = std::clamp(inV, 0.0f, outV);

        innerRad = inV;
        outerRad = outV;
    }

    static GpuLight makeDirectionalWorld(const glm::vec3& dirWorld,
                                         const glm::vec3& color,
                                         float            intensity,
                                         float            softnessRadians) noexcept
    {
        GpuLight gl = {};
        gl.pos_type = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(GpuLightType::Directional));

        const glm::vec3 fwdWorld = un::safe_normalize(dirWorld, glm::vec3(0.0f, 0.0f, -1.0f));

        gl.dir_range       = glm::vec4(fwdWorld, std::max(0.0f, softnessRadians));
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(0.0f);

        return gl;
    }

    static GpuLight makePointWorld(const glm::vec3& posWorld,
                                   const glm::vec3& color,
                                   float            intensity,
                                   float            range,
                                   float            softnessRadians) noexcept
    {
        GpuLight gl        = {};
        gl.pos_type        = glm::vec4(posWorld, static_cast<float>(GpuLightType::Point));
        gl.dir_range       = glm::vec4(0.0f, 0.0f, 0.0f, std::max(0.0f, range));
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));

        // x/y unused for point. z = RT soft-shadow angular radius.
        gl.spot_params = glm::vec4(0.0f, 0.0f, std::max(0.0f, softnessRadians), 0.0f);
        return gl;
    }

    static GpuLight makeSpotWorld(const glm::vec3& posWorld,
                                  const glm::vec3& dirWorld,
                                  const glm::vec3& color,
                                  float            intensity,
                                  float            range,
                                  float            innerConeRad,
                                  float            outerConeRad,
                                  float            softnessRadians) noexcept
    {
        const float inner = std::max(0.0f, innerConeRad);
        const float outer = std::max(inner, outerConeRad);

        float innerCos = std::cos(inner);
        float outerCos = std::cos(outer);

        if (innerCos < outerCos)
            std::swap(innerCos, outerCos);

        GpuLight gl        = {};
        gl.pos_type        = glm::vec4(posWorld, static_cast<float>(GpuLightType::Spot));
        gl.dir_range       = glm::vec4(un::safe_normalize(dirWorld, glm::vec3(0.0f, 0.0f, -1.0f)),
                                 std::max(0.0f, range));
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));

        // x=innerCos, y=outerCos, z=RT soft-shadow angular radius, w unused
        gl.spot_params = glm::vec4(innerCos, outerCos, std::max(0.0f, softnessRadians), 0.0f);
        return gl;
    }

    static glm::vec3 viewportForwardWorld(const Viewport& vp) noexcept
    {
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 fwd  = glm::vec3(invV * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        return un::safe_normalize(fwd, glm::vec3(0.0f, 0.0f, -1.0f));
    }

    static glm::vec3 viewportRightWorld(const Viewport& vp) noexcept
    {
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 rgt  = glm::vec3(invV * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        return un::safe_normalize(rgt, glm::vec3(1.0f, 0.0f, 0.0f));
    }

    static glm::vec3 viewportUpWorld(const Viewport& vp) noexcept
    {
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 up   = glm::vec3(invV * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        return un::safe_normalize(up, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    static glm::vec3 viewportPosWorld(const Viewport& vp) noexcept
    {
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 p    = glm::vec3(invV * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
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
                break;
        }
        return LightingSettings::ModePolicy::Both;
    }

    static bool allowHeadlight(const LightingSettings& s, DrawMode mode) noexcept
    {
        if (!s.useHeadlight)
            return false;

        const LightingSettings::ModePolicy p = policyForDrawMode(s, mode);
        return (p == LightingSettings::ModePolicy::HeadlightOnly || p == LightingSettings::ModePolicy::Both);
    }

    static bool allowSceneLights(const LightingSettings& s, DrawMode mode) noexcept
    {
        if (!s.useSceneLights)
            return false;

        const LightingSettings::ModePolicy p = policyForDrawMode(s, mode);
        return (p == LightingSettings::ModePolicy::SceneOnly || p == LightingSettings::ModePolicy::Both);
    }

} // namespace

//============================================================
// buildGpuLightsUBO()  (FULL REPLACEMENT)
//============================================================
void buildGpuLightsUBO(const LightingSettings&  settings,
                       const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept
{
    constexpr bool kLogSceneLights = true;

    out         = {};
    out.info    = glm::uvec4(0u);
    out.ambient = glm::vec4(0.0f);

    const DrawMode dm = vp.drawMode();

    if constexpr (kLogSceneLights)
    {
        const char* dmStr = "Unknown";
        switch (dm)
        {
            case DrawMode::SOLID:
                dmStr = "SOLID";
                break;
            case DrawMode::SHADED:
                dmStr = "SHADED";
                break;
            case DrawMode::RAY_TRACE:
                dmStr = "RAY_TRACE";
                break;
            default:
                break;
        }

        std::printf("\n=== buildGpuLightsUBO() drawMode=%s ===\n", dmStr);
        std::printf("settings: useHeadlight=%d useSceneLights=%d ambientFill=%.3f exposureUI=%.3f tonemap=%d\n",
                    settings.useHeadlight ? 1 : 0,
                    settings.useSceneLights ? 1 : 0,
                    settings.ambientFill,
                    settings.exposure,
                    settings.tonemap ? 1 : 0);
    }

    // ------------------------------------------------------------
    // 1) Headlight (optional)
    // ------------------------------------------------------------
    if (allowHeadlight(settings, dm) && headlight.enabled && headlight.intensity > 0.0f)
    {
        if (kHeadlightAsFlashlightSpot)
        {
            const glm::vec3 posWorld = viewportPosWorld(vp);
            const glm::vec3 dirWorld = viewportForwardWorld(vp);

            // NOTE: spot_params.z used by RT for soft shadows (optional)
            pushLight(out, makeSpotWorld(posWorld, dirWorld, headlight.color, headlight.intensity, kHeadlightRange, kHeadlightInnerRad, kHeadlightOuterRad, kSceneSpotSoftnessRadians));

            if constexpr (kLogSceneLights)
                std::printf("Headlight: FLASHLIGHT SPOT  I=%.3f\n", headlight.intensity);
        }
        else
        {
            glm::vec3 dirWorld = {};

            if (kUseFixedSunForHeadlight)
            {
                dirWorld = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f));
            }
            else
            {
                const glm::vec3 forward = viewportForwardWorld(vp);

                if (kUseHeadlightBias &&
                    (std::abs(kHeadlightSideBias) > 0.0f || std::abs(kHeadlightUpBias) > 0.0f))
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

            pushLight(out, makeDirectionalWorld(dirWorld, headlight.color, headlight.intensity, kHeadlightSoftnessRadians));

            if constexpr (kLogSceneLights)
            {
                std::printf("Headlight: DIRECTIONAL  I=%.3f dirW=(%.3f %.3f %.3f)\n",
                            headlight.intensity,
                            dirWorld.x,
                            dirWorld.y,
                            dirWorld.z);
            }
        }
    }

    // ------------------------------------------------------------
    // 2) Scene lights (optional)
    // ------------------------------------------------------------
    uint32_t sceneLightCount = 0u;
    float    maxSceneLight   = 0.0f; // logging only

    const float ptIntMul  = clampNonNeg(settings.scenePointIntensityMul, 1.0f);
    const float ptRngMul  = clampPos(settings.scenePointRangeMul, 1.0f);
    const float spIntMul  = clampNonNeg(settings.sceneSpotIntensityMul, 1.0f);
    const float spRngMul  = clampPos(settings.sceneSpotRangeMul, 1.0f);
    const float spConeMul = clampPos(settings.sceneSpotConeMul, 1.0f);

    if (allowSceneLights(settings, dm) && scene)
    {
        const LightHandler* lh = scene->lightHandler();
        if (lh)
        {
            const auto ids = lh->allLights();

            if constexpr (kLogSceneLights)
                std::printf("Scene lights: enabled count=%zu\n", ids.size());

            for (LightId id : ids)
            {
                const Light* l = lh->light(id);
                if (!l || !l->enabled)
                    continue;

                float intensity = std::max(0.0f, l->intensity);
                float range     = std::max(0.0f, l->range);

                if (l->type == LightType::Point)
                {
                    intensity *= ptIntMul;
                    range *= ptRngMul;
                }
                else if (l->type == LightType::Spot)
                {
                    intensity *= spIntMul;
                    range *= spRngMul;
                }

                // Logging metric only (NOT used for exposure)
                const glm::vec3 c01  = clamp01(l->color);
                const float     cmax = std::max(c01.x, std::max(c01.y, c01.z));
                maxSceneLight        = std::max(maxSceneLight, intensity * cmax);
                ++sceneLightCount;

                if constexpr (kLogSceneLights)
                {
                    const char* typeStr = (l->type == LightType::Directional) ? "Directional"
                                          : (l->type == LightType::Point)     ? "Point"
                                                                              : "Spot";
                    std::printf("  Light %-11s  I=%.3f  range=%.3f  color=(%.3f %.3f %.3f)\n",
                                typeStr,
                                intensity,
                                range,
                                c01.x,
                                c01.y,
                                c01.z);
                }

                switch (l->type)
                {
                    case LightType::Directional:
                        pushLight(out, makeDirectionalWorld(l->direction, l->color, intensity, 0.0f));
                        break;

                    case LightType::Point:
                        pushLight(out, makePointWorld(l->position, l->color, intensity, range, kScenePointSoftnessRadians));
                        break;

                    case LightType::Spot: {
                        float innerRad = l->spotInnerConeRad;
                        float outerRad = l->spotOuterConeRad;
                        scaleSpotCones(innerRad, outerRad, spConeMul);

                        pushLight(out, makeSpotWorld(l->position, l->direction, l->color, intensity, range, innerRad, outerRad, kSceneSpotSoftnessRadians));
                        break;
                    }
                }

                if (out.info.x >= kMaxGpuLights)
                    break;
            }
        }
    }
    else
    {
        if constexpr (kLogSceneLights)
            std::printf("Scene lights: skipped (policy or no scene)\n");
    }

    // ------------------------------------------------------------
    // 3) Ambient.rgb (fill) and Ambient.a (exposure scalar)
    // ------------------------------------------------------------
    const float fill = std::max(0.0f, settings.ambientFill);
    out.ambient      = glm::vec4(fill, fill, fill, 0.0f);

    const float exposureMul = std::max(0.0f, settings.exposure);

    float exposureScalar = kBaseExposureScalar * exposureMul;
    exposureScalar       = std::clamp(exposureScalar, kMinFinalExposure, kMaxFinalExposure);

    out.ambient.a = exposureScalar;

    if constexpr (kLogSceneLights)
    {
        std::printf("Exposure: base=%.6f mul=%.6f => exposureScalar=%.6f\n",
                    kBaseExposureScalar,
                    exposureMul,
                    out.ambient.a);

        std::printf("Scene lights: pushed=%u (seen=%u) maxSceneLight(log)=%.3f\n",
                    out.info.x,
                    sceneLightCount,
                    maxSceneLight);
    }
}
