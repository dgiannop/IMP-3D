// ============================================================
// GpuLights.cpp
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

    // If true, inject the headlight as a VIEW-SPACE spotlight at the camera
    // (a "flashlight" that always follows the view).
    constexpr bool kHeadlightAsFlashlightSpot = false;

    // Headlight bias (camera-locked directional):
    // Keep this subtle for SOLID. In your tests, positive "up" tended to darken
    // the top face, so default up bias is zero (or slightly negative).
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

    constexpr float kEps = 1e-8f;

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

    static glm::vec3 viewPos(const glm::mat4& V, const glm::vec3& pWorld) noexcept
    {
        const glm::vec4 pv = V * glm::vec4(pWorld, 1.0f);
        return glm::vec3(pv.x, pv.y, pv.z);
    }

    static glm::vec3 viewDir(const glm::mat4& V, const glm::vec3& dWorld) noexcept
    {
        const glm::vec4 dv = V * glm::vec4(dWorld, 0.0f);
        return un::safe_normalize(glm::vec3(dv.x, dv.y, dv.z), glm::vec3(0, 0, -1));
    }

    static GpuLight makeDirectionalView(const glm::vec3& dirView, const glm::vec3& color, float intensity) noexcept
    {
        GpuLight gl{};
        gl.pos_type = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(GpuLightType::Directional));

        const glm::vec3 fwdView = un::safe_normalize(dirView, glm::vec3(0, 0, -1));

        // xyz = forward (VIEW space), w = softness (angular radius in radians)
        gl.dir_range = glm::vec4(fwdView, kHeadlightSoftnessRadians);

        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(0.0f);

        return gl;
    }

    static GpuLight makePointView(const glm::vec3& posView, const glm::vec3& color, float intensity, float range) noexcept
    {
        GpuLight gl{};
        gl.pos_type        = glm::vec4(posView, static_cast<float>(GpuLightType::Point));
        gl.dir_range       = glm::vec4(0.0f, 0.0f, 0.0f, std::max(0.0f, range));
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(0.0f);
        return gl;
    }

    static GpuLight makeSpotView(const glm::vec3& posView,
                                 const glm::vec3& dirView,
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
        gl.pos_type        = glm::vec4(posView, static_cast<float>(GpuLightType::Spot));
        gl.dir_range       = glm::vec4(un::safe_normalize(dirView, glm::vec3(0, 0, -1)), std::max(0.0f, range));
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

    const glm::mat4 V  = vp.view(); // WORLD -> VIEW
    const DrawMode  dm = vp.drawMode();

    // ------------------------------------------------------------
    // 1) Modeling headlight (optional)
    // ------------------------------------------------------------
    // Render-time "modeling light" specified in VIEW space. This is not a SceneLight.
    // Controlled via LightingSettings + HeadlightSettings.
    if (allowHeadlight(settings, dm) && headlight.enabled && headlight.intensity > 0.0f)
    {
        if (kHeadlightAsFlashlightSpot)
        {
            // "Flashlight" headlight (debug):
            // - Positioned at the camera origin in VIEW space (0,0,0).
            // - Points forward along -Z in VIEW space.
            const glm::vec3 posView       = glm::vec3(0.0f);
            const glm::vec3 dirTowardView = glm::vec3(0.0f, 0.0f, -1.0f);

            pushLight(out,
                      makeSpotView(posView,
                                   dirTowardView,
                                   headlight.color,
                                   headlight.intensity,
                                   kHeadlightRange,
                                   kHeadlightInnerRad,
                                   kHeadlightOuterRad));
        }
        else
        {
            // Directional modeling headlight:
            // - Default: camera-locked (derived from the viewport basis).
            // - Optional: very subtle bias while preserving the WORLD->VIEW conversion
            //   path (this keeps your SOLID look stable).
            glm::vec3 dirWorld = {};

            if (kUseFixedSunForHeadlight)
            {
                dirWorld = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f)); // fixed sun (debug)
            }
            else
            {
                const glm::vec3 forward = viewportForwardWorld(vp);

                if (kUseHeadlightBias && (std::abs(kHeadlightSideBias) > 0.0f || std::abs(kHeadlightUpBias) > 0.0f))
                {
                    // VIEW->WORLD basis from the camera.
                    const glm::mat4 invV = glm::inverse(vp.view());
                    const glm::vec3 right =
                        un::safe_normalize(glm::vec3(invV * glm::vec4(1, 0, 0, 0)), glm::vec3(1, 0, 0));
                    const glm::vec3 up = un::safe_normalize(glm::vec3(invV * glm::vec4(0, 1, 0, 0)), glm::vec3(0, 1, 0));

                    const float side = kHeadlightSideBias * kHeadlightBiasScale;
                    const float upb  = kHeadlightUpBias * kHeadlightBiasScale;

                    dirWorld = un::safe_normalize(forward + (side * right) + (upb * up), forward);
                }
                else
                {
                    dirWorld = forward;
                }
            }

            const glm::vec3 dirView = viewDir(V, dirWorld);
            pushLight(out, makeDirectionalView(dirView, headlight.color, headlight.intensity));
        }
    }

    // ------------------------------------------------------------
    // 2) Scene lights (optional)
    // ------------------------------------------------------------
    // Scene lights live in WORLD space. We convert to VIEW space here for shaders.
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

                // Exposure estimation uses intensity * max(color channel).
                // This roughly tracks peak radiance contribution for default scenes.
                const float cmax = std::max(l->color.x, std::max(l->color.y, l->color.z));
                maxSceneLight    = std::max(maxSceneLight, std::max(0.0f, l->intensity) * std::max(0.0f, cmax));
                ++sceneLightCount;

                switch (l->type)
                {
                    case LightType::Directional: {
                        const glm::vec3 dirV = viewDir(V, l->direction);
                        pushLight(out, makeDirectionalView(dirV, l->color, l->intensity));
                    }
                    break;

                    case LightType::Point: {
                        const glm::vec3 posV = viewPos(V, l->position);
                        pushLight(out, makePointView(posV, l->color, l->intensity, l->range));
                    }
                    break;

                    case LightType::Spot: {
                        const glm::vec3 posV = viewPos(V, l->position);
                        const glm::vec3 dirV = viewDir(V, l->direction);
                        pushLight(out,
                                  makeSpotView(posV,
                                               dirV,
                                               l->color,
                                               l->intensity,
                                               l->range,
                                               l->spotInnerConeRad,
                                               l->spotOuterConeRad));
                    }
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
    // ambient.rgb is a small "fill" term (optional) and is not physically based.
    // We expose it as a UI knob for modeling convenience.
    const float fill = std::max(0.0f, settings.ambientFill);
    out.ambient      = glm::vec4(fill, fill, fill, 0.0f);

    // Exposure:
    // - Keep the existing simple auto-exposure behavior based on scene lights.
    // - Multiply by settings.exposure so the UI can bias brighter/darker without
    //   completely changing the auto logic.
    float exposure = 1.0f;

    if (sceneLightCount > 0u && maxSceneLight > 0.0f)
    {
        // Key value: 0.18 is classic middle grey.
        constexpr float kKey = 0.18f;

        exposure = kKey / maxSceneLight;

        // Clamp to sane bounds so extreme files don't go totally black/white.
        exposure = std::clamp(exposure, 1e-6f, 2.0f);
    }

    // User bias (defaults to 1.0).
    exposure *= std::max(0.0f, settings.exposure);

    out.ambient.a = exposure;
}
