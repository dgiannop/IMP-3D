// ============================================================
// GpuLights.cpp  (WORLD-space LightsUBO: directional/point/spot)
// ============================================================
//
// Conventions:
//   - All lights in GpuLightsUBO are expressed in WORLD space.
//     * Directional: lights[i].direction = forward (WORLD)
//     * Point:       lights[i].position  = position (WORLD)
//     * Spot:        lights[i].position  = position (WORLD)
//                   lights[i].direction = forward (WORLD)
//   - lights[i].range:
//     * Directional: angular radius (radians) for soft shadows (optional; 0 = hard)
//     * Point/Spot:  range (world units), 0 = inverse-square only
//
// RT-only soft shadows for point/spot:
//   - lights[i].spot_params.z stores angular radius (radians) for soft shadows.
//     (Directional softness stays in range.)
//
// IMPORTANT:
//   - out.ambient      = ambient fill color (already scaled by ambientFill)
//   - out.exposure     = exposure scalar used by shaders
//   - out.count        = number of active lights
//
// Production/editor behavior (this file):
//   - NO auto-exposure derived from max light intensity.
//   - Exposure is controlled as EV100 (photometric):
//       exposureScalar = 2^(-EV100)
//     Recommended default EV100 ~= 5.64 to match the old baseline 0.02.
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
    // Exposure (photometric EV100)
    // ------------------------------------------------------------
    // exposureScalar = 2^(-EV100)
    // Old baseline exposureScalar ~ 0.02 corresponds to EV100 ~= 5.643856.
    constexpr float kDefaultEv100 = 5.64f;

    // UI sanity clamp for EV100 (editor)
    constexpr float kMinEv100 = -10.0f;
    constexpr float kMaxEv100 = 24.0f;

    // Safety clamp for exposureScalar (prevents accidental huge values)
    constexpr float kMinExposureScalar = 0.0f;
    constexpr float kMaxExposureScalar = 64.0f;

    static glm::vec3 clamp01(const glm::vec3& c) noexcept
    {
        return glm::vec3(std::clamp(c.x, 0.0f, 1.0f),
                         std::clamp(c.y, 0.0f, 1.0f),
                         std::clamp(c.z, 0.0f, 1.0f));
    }

    static std::uint32_t lightCount(const GpuLightsUBO& ubo) noexcept
    {
        return ubo.count;
    }

    static void setLightCount(GpuLightsUBO& ubo, std::uint32_t v) noexcept
    {
        ubo.count = v;
    }

    static void pushLight(GpuLightsUBO& out, const GpuLight& l) noexcept
    {
        const std::uint32_t count = lightCount(out);
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

    // ------------------------------------------------------------
    // GpuLight builders (WORLD space)
    // ------------------------------------------------------------
    static GpuLight makeDirectionalWorld(const glm::vec3& dirWorld,
                                         const glm::vec3& color,
                                         float            intensity,
                                         float            softnessRadians) noexcept
    {
        GpuLight gl = {};

        gl.position = glm::vec3(0.0f); // unused for directional
        gl.type     = static_cast<std::uint32_t>(GpuLightType::Directional);

        const glm::vec3 fwdWorld = un::safe_normalize(dirWorld, glm::vec3(0.0f, 0.0f, -1.0f));

        gl.direction = fwdWorld;
        // For directional lights, range stores angular radius (soft shadow radius).
        gl.range = std::max(0.0f, softnessRadians);

        gl.color     = clamp01(color);
        gl.intensity = std::max(0.0f, intensity);

        gl.spot_params = glm::vec4(0.0f); // not used for directional
        return gl;
    }

    static GpuLight makePointWorld(const glm::vec3& posWorld,
                                   const glm::vec3& color,
                                   float            intensity,
                                   float            range,
                                   float            softnessRadians) noexcept
    {
        GpuLight gl = {};

        gl.position = posWorld;
        gl.type     = static_cast<std::uint32_t>(GpuLightType::Point);

        gl.direction = glm::vec3(0.0f); // unused for pure point
        // For point lights, range is distance falloff range.
        gl.range = std::max(0.0f, range);

        gl.color     = clamp01(color);
        gl.intensity = std::max(0.0f, intensity);

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

        GpuLight gl = {};

        gl.position = posWorld;
        gl.type     = static_cast<std::uint32_t>(GpuLightType::Spot);

        gl.direction = un::safe_normalize(dirWorld, glm::vec3(0.0f, 0.0f, -1.0f));
        gl.range     = std::max(0.0f, range);

        gl.color     = clamp01(color);
        gl.intensity = std::max(0.0f, intensity);

        // x=innerCos, y=outerCos, z=RT soft-shadow angular radius, w unused
        gl.spot_params = glm::vec4(innerCos, outerCos, std::max(0.0f, softnessRadians), 0.0f);
        return gl;
    }

    // ------------------------------------------------------------
    // Viewport helpers
    // ------------------------------------------------------------
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

    static LightingSettings::ModePolicy policyForDrawMode(const LightingSettings& s,
                                                          DrawMode                mode) noexcept
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

    static float exposureScalarFromEv100(float ev100) noexcept
    {
        if (!std::isfinite(ev100))
            ev100 = kDefaultEv100;

        ev100 = std::clamp(ev100, kMinEv100, kMaxEv100);

        // exposureScalar = 2^(-EV100)
        float s = std::exp2(-ev100);

        if (!std::isfinite(s))
            s = std::exp2(-kDefaultEv100);

        s = std::clamp(s, kMinExposureScalar, kMaxExposureScalar);
        return s;
    }

} // namespace

//============================================================
// buildGpuLightsUBO()
//============================================================
void buildGpuLightsUBO(const LightingSettings&  settings,
                       const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept
{
    constexpr bool kLogSceneLights = true;

    // Zero everything; then explicitly reset the header fields for clarity.
    out          = {};
    out.count    = 0u;
    out.ambient  = glm::vec3(0.0f);
    out.exposure = 0.0f;

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

        std::printf("settings: useHeadlight=%d useSceneLights=%d ambientFill=%.3f exposureEV100=%.3f tonemap=%d\n",
                    settings.useHeadlight ? 1 : 0,
                    settings.useSceneLights ? 1 : 0,
                    settings.ambientFill,
                    settings.exposureEv100,
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
    std::uint32_t sceneLightCount = 0u;
    float         maxSceneLight   = 0.0f; // logging only

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
                    std::printf("%-11s Light:   I=%.3f  range=%.3f  color=(%.3f %.3f %.3f)\n",
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

                if (out.count >= kMaxGpuLights)
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
    // 3) Ambient.rgb (fill) and exposure scalar
    // ------------------------------------------------------------
    const float fill = std::max(0.0f, settings.ambientFill);
    out.ambient      = glm::vec3(fill);

    const float exposureScalar = exposureScalarFromEv100(settings.exposureEv100);
    out.exposure               = exposureScalar;

    if constexpr (kLogSceneLights)
    {
        std::printf("Exposure(EV100): ev=%.3f => exposureScalar=%.6f\n",
                    settings.exposureEv100,
                    out.exposure);

        std::printf("Scene lights: pushed=%u (seen=%u) maxSceneLight(log)=%.3f\n",
                    out.count,
                    sceneLightCount,
                    maxSceneLight);
    }
}
