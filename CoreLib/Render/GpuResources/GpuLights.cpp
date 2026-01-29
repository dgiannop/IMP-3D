// ============================================================
// GpuLights.cpp (or wherever buildGpuLightsUBO lives)
// ============================================================

#include "GpuLights.hpp"

#include <algorithm>
#include <cmath>

#include "Light.hpp"
#include "LightHandler.hpp"
#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    constexpr bool kUseFixedSunForHeadlight = false; // set true for RT shadow testing

    constexpr float kEps = 1e-8f;

    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept
    {
        const float len2 = glm::dot(v, v);
        if (len2 <= kEps)
            return fallback;
        return v * (1.0f / std::sqrt(len2));
    }

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
        return safeNormalize(glm::vec3(dv.x, dv.y, dv.z), glm::vec3(0, 0, -1));
    }

    static GpuLight makeDirectionalView(const glm::vec3& dirView, const glm::vec3& color, float intensity) noexcept
    {
        GpuLight gl{};
        gl.pos_type = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(GpuLightType::Directional));

        const glm::vec3 fwdView = safeNormalize(dirView, glm::vec3(0, 0, -1));

        // xyz = forward (VIEW space), w = softness (angular radius in radians)
        constexpr float kSoftnessRadians = 0.05f; // ~3 degrees, tweak later
        gl.dir_range                     = glm::vec4(fwdView, kSoftnessRadians);

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

        GpuLight gl{};
        gl.pos_type        = glm::vec4(posView, static_cast<float>(GpuLightType::Spot));
        gl.dir_range       = glm::vec4(safeNormalize(dirView, glm::vec3(0, 0, -1)), std::max(0.0f, range));
        gl.color_intensity = glm::vec4(clamp01(color), std::max(0.0f, intensity));
        gl.spot_params     = glm::vec4(innerCos, outerCos, 0.0f, 0.0f);
        return gl;
    }

    static glm::vec3 viewportForwardWorld(const Viewport& vp) noexcept
    {
        // Assumes vp.view() is WORLD->VIEW
        const glm::mat4 invV = glm::inverse(vp.view());
        const glm::vec3 fwd  = glm::vec3(invV * glm::vec4(0, 0, -1, 0));
        return safeNormalize(fwd, glm::vec3(0, 0, -1));
    }

} // namespace

void buildGpuLightsUBO(const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept
{
    out         = {};
    out.info    = glm::uvec4(0u);
    out.ambient = glm::vec4(0.f);

    const glm::mat4 V = vp.view(); // WORLD -> VIEW

    // ------------------------------------------------------------
    // 1) Modeling light first (optional)
    // ------------------------------------------------------------
    if (headlight.enabled)
    {
        glm::vec3 dirWorld = {};

        if (kUseFixedSunForHeadlight)
            dirWorld = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f)); // fixed sun
        else
            dirWorld = viewportForwardWorld(vp); // headlight

        const glm::vec3 dirView = viewDir(V, dirWorld);
        pushLight(out, makeDirectionalView(dirView, headlight.color, headlight.intensity));
    }

    // ------------------------------------------------------------
    // 2) Scene lights
    // ------------------------------------------------------------
    uint32_t sceneLightCount = 0u;
    float    maxSceneLight   = 0.0f;

    if (scene)
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

                // For exposure estimation: use intensity * max(color channel)
                // (This matches how your shader scales radiance.)
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
    // Keep ambient minimal (optional). This is NOT exposure.
    // For pure physically-based: leave rgb=0.
    const glm::vec3 modelingAmbientRgb(0.002f); // or e.g. glm::vec3(0.02f)
    out.ambient = glm::vec4(modelingAmbientRgb, 0.0f);

    // Exposure:
    // - If there are scene lights, use a "middle-grey" style key / maxLight.
    // - If there are NO scene lights, keep exposure = 1 so headlight-only scenes look normal.
    float exposure = 1.0f;

    if (sceneLightCount > 0u && maxSceneLight > 0.0f)
    {
        // Key value: tweakable. 0.18 is classic middle grey.
        // For a slightly brighter default, try 0.25.
        constexpr float kKey = 0.18f;

        exposure = kKey / maxSceneLight;

        // Clamp to sane bounds so extreme files don't go totally black/white.
        exposure = std::clamp(exposure, 1e-6f, 2.0f);
    }

    out.ambient.a = exposure;
}
