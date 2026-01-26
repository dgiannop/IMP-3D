#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "GpuLight.hpp"

class Scene;
class Viewport;

constexpr uint32_t kMaxGpuLights = 64;

/**
 * @brief std140-friendly UBO payload for lights.
 *
 * Shader assumes all lights are provided in VIEW SPACE.
 */
struct alignas(16) GpuLightsUBO
{
    // x = lightCount, yzw unused (std140 alignment)
    glm::uvec4 info = {};

    // rgb = ambient, a = ambientStrength (optional)
    glm::vec4 ambient = {};

    GpuLight lights[kMaxGpuLights] = {};
};

static_assert(alignof(GpuLightsUBO) == 16);

/**
 * @brief Simple render-time headlight (modeling light) controlled from C++.
 * This is NOT a SceneLight; it follows the camera by being specified in VIEW SPACE.
 */
struct HeadlightSettings
{
    bool      enabled   = true;
    glm::vec3 dirVS     = glm::vec3(0.3f, 0.7f, 0.2f); // view-space direction
    glm::vec3 color     = glm::vec3(1.0f);
    float     intensity = 1.0f;
};

/**
 * @brief Build a per-viewport VIEW-SPACE light UBO by mixing headlight + (future) Scene lights.
 *
 * Notes:
 *  - This function is renderer-oriented because it depends on the viewport view matrix.
 *  - Scene lights are expected to be in WORLD space and will be transformed to VIEW space here.
 */
void buildGpuLightsUBO(const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept;
