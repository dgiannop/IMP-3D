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
 *
 * Layout (matches std140 in GLSL):
 *   - count: number of active lights (<= kMaxGpuLights)
 *   - pad0/1/2: explicit padding to keep 16-byte alignment
 *   - ambient: rgb ambient fill color
 *   - exposure: scalar used by shaders for exposure mapping
 *   - lights[]: array of GpuLight structs in WORLD SPACE
 */
struct alignas(16) GpuLightsUBO
{
    // Number of active lights (0..kMaxGpuLights).
    // Explicit pads for std140 alignment (4 * uint = 16 bytes).
    std::uint32_t count = 0u;
    std::uint32_t pad0  = 0u;
    std::uint32_t pad1  = 0u;
    std::uint32_t pad2  = 0u;

    // rgb = ambient fill color, exposure = scalar used by shaders
    glm::vec3 ambient  = glm::vec3(0.0f);
    float     exposure = 0.0f;

    GpuLight lights[kMaxGpuLights] = {};
};

static_assert(alignof(GpuLightsUBO) == 16, "GpuLightsUBO must be 16-byte aligned");

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
