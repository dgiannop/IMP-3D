#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// ============================================================
// GPU Lights (WORLD-space)
// ============================================================
//
// This file defines the GPU-facing light packing used by both
// raster and ray tracing shaders.
//
// Conventions (WORLD space):
//   - position:
//       * Point/Spot: light position in WORLD space
//       * Directional: unused (0)
//
//   - type:
//       * Encodes GpuLightType as uint (0 = Directional, 1 = Point, 2 = Spot)
//
//   - direction:
//       * Directional: light "forward" direction in WORLD space (normalized)
//       * Spot:        light "forward" direction in WORLD space (normalized)
//       * Point:       unused (0)
//
//   - range:
//       * Directional: angular radius (radians) for soft shadows (0 = hard)
//       * Point/Spot:  range (world units), 0 = inverse-square only
//
//   - color:
//       * Light color (usually clamped 0..1 when packed)
//
//   - intensity:
//       * Light intensity (scene units; may be large for imported content)
//
//   - spot_params:
//       * x = innerCos, y = outerCos (both derived from cone angles)
//       * z = RT soft-shadow angular radius (point/spot)
//       * w = reserved
//
// Exposure / tonemapping note:
//   - Exposure is NOT a light parameter.
//   - It is carried in GpuLightsUBO.exposure as a frame/camera scalar.
// ============================================================

enum class GpuLightType : std::uint32_t
{
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

/**
 * GPU-facing light data.
 * std140-friendly packing (vec3 + uint, etc.).
 *
 * IMPORTANT:
 * - All vectors are in WORLD space (see file header for conventions).
 * - This is a packed GPU struct; the engine-side Light model can be richer.
 */
struct alignas(16) GpuLight
{
    // xyz = position (WORLD) for point/spot, unused for directional
    glm::vec3     position = glm::vec3(0.0f);
    std::uint32_t type     = 0u; // GpuLightType as uint

    // xyz = forward dir (WORLD) for directional/spot, unused for point
    // range:
    //   - directional: angular radius (radians) for soft shadows (0 = hard)
    //   - point/spot:  range in WORLD units (0 = inverse-square only)
    glm::vec3 direction = glm::vec3(0.0f);
    float     range     = 0.0f;

    // rgb = color (0..1), intensity = light strength (scene units)
    glm::vec3 color     = glm::vec3(0.0f);
    float     intensity = 0.0f;

    // x = spot inner cos, y = spot outer cos, z = RT soft-shadow angular radius, w reserved
    glm::vec4 spot_params = glm::vec4(0.0f);
};

static_assert(alignof(GpuLight) == 16, "GpuLight must be 16-byte aligned");
static_assert(sizeof(GpuLight) % 16 == 0, "GpuLight size should be a multiple of 16 bytes");
