#pragma once

#include <cstdint>
#include <glm/vec4.hpp>

// ============================================================
// GPU Lights (WORLD-space)
// ============================================================
//
// This file defines the GPU-facing light packing used by both
// raster and ray tracing shaders.
//
// Conventions (WORLD space):
//   - pos_type.xyz:
//       * Point/Spot: light position in WORLD space
//       * Directional: unused (0)
//   - pos_type.w:
//       * Light type (GpuLightType as float)
//
//   - dir_range.xyz:
//       * Directional: light "forward" direction in WORLD space (normalized)
//       * Spot:        light "forward" direction in WORLD space (normalized)
//       * Point:       unused (0)
//
//   - dir_range.w:
//       * Directional: angular radius (radians) for soft shadows (0 = hard)
//       * Point/Spot:  range (world units), 0 = inverse-square only
//
//   - color_intensity.rgb:
//       * Light color (clamped 0..1 when packed)
//   - color_intensity.a:
//       * Light intensity (scene units; may be large for imported content)
//
//   - spot_params:
//       * x = innerCos, y = outerCos (both derived from cone angles)
//       * z/w reserved
//
// Exposure / tonemapping note:
//   - Exposure is NOT a light parameter.
//   - It is carried in LightsUBO.ambient.a as a frame/camera scalar.
// ============================================================

enum class GpuLightType : uint32_t
{
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

/**
 * GPU-facing light data.
 * std140-friendly packing (vec4s).
 *
 * IMPORTANT:
 * - All vectors are in WORLD space (see file header for conventions).
 * - This is a packed GPU struct; the engine-side Light model can be richer.
 */
struct GpuLight
{
    // xyz = position (WORLD) for point/spot, w = type
    glm::vec4 pos_type = {};

    // xyz = forward dir (WORLD) for directional/spot, w = range (point/spot) or angular radius (directional)
    glm::vec4 dir_range = {};

    // rgb = color (0..1), a = intensity
    glm::vec4 color_intensity = {};

    // x = spot inner cos, y = spot outer cos, z/w reserved
    glm::vec4 spot_params = {};
};
