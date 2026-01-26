#pragma once

#include <cstdint>
#include <glm/vec4.hpp>

enum class GpuLightType : uint32_t
{
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

/**
 * GPU-facing light data. Assumed to be in VIEW SPACE when consumed by shaders.
 * std140-friendly packing (vec4s).
 */
struct GpuLight
{
    // xyz = position (view space) for point/spot, w = type
    glm::vec4 pos_type = {};

    // xyz = direction (view space, normalized) for directional/spot, w = range (point/spot) or unused
    glm::vec4 dir_range = {};

    // rgb = color, a = intensity
    glm::vec4 color_intensity = {};

    // x = spot inner cos, y = spot outer cos, z/w reserved
    glm::vec4 spot_params = {};
};
