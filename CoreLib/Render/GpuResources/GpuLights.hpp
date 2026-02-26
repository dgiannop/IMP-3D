#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "GpuLight.hpp"
#include "LightingSettings.hpp"

class Scene;
class Viewport;

constexpr uint32_t kMaxGpuLights = 64;

/**
 * @brief std140-friendly UBO payload for lights.
 *
 * All lights are provided in WORLD SPACE.
 * Shaders operate entirely in world space.
 */
struct alignas(16) GpuLightsUBO
{
    // x = lightCount, yzw unused (std140 alignment)
    glm::uvec4 info = {};

    // rgb = ambient fill color, a = exposure scalar
    glm::vec4 ambient = {};

    GpuLight lights[kMaxGpuLights] = {};
};

static_assert(alignof(GpuLightsUBO) == 16);

/**
 * @brief Simple render-time headlight (modeling light) driven by the camera.
 *
 * The headlight follows the active viewport camera; direction/position
 * are derived from the viewport in WORLD SPACE.
 *
 * This is NOT a SceneLight; it is injected at render time.
 */
struct HeadlightSettings
{
    bool      enabled   = true;
    glm::vec3 color     = glm::vec3(1.0f);
    float     intensity = 1.0f;
};

/**
 * @brief Build a per-viewport WORLD-SPACE light UBO by mixing headlight and Scene lights.
 *
 * Notes:
 *  - Scene lights are expected to already be in WORLD SPACE.
 *  - The headlight is derived from the viewport camera in WORLD SPACE.
 *  - No view-space transforms are performed here.
 */
void buildGpuLightsUBO(const LightingSettings&  settings,
                       const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept;
