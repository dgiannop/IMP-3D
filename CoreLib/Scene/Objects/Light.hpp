//============================================================
// Light.hpp
//============================================================
#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <string>

using LightId                     = int32_t; // stable index from HoleList
constexpr LightId kInvalidLightId = -1;

enum class LightType : uint32_t
{
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

/**
 * @brief Scene light (WORLD SPACE).
 *
 * This is the authoritative, serializable light description owned by LightHandler.
 * It is intentionally simple and undo-friendly:
 *  - no SceneObject inheritance
 *  - no transform matrix (position + direction is enough for now)
 *
 * The renderer is responsible for converting these WORLD-space lights into the
 * GPU format it needs (typically VIEW-space).
 *
 * Notes:
 *  - Directional: uses direction, ignores position and range.
 *  - Point: uses position, uses range if > 0.
 *  - Spot: uses position + direction + range, uses cone angles.
 */
struct Light
{
    LightId     id   = kInvalidLightId;
    std::string name = {};

    LightType type = LightType::Point;

    // ------------------------------------------------------------
    // World-space placement
    // ------------------------------------------------------------
    glm::vec3 position  = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f); // should be normalized

    // ------------------------------------------------------------
    // Emission
    // ------------------------------------------------------------
    glm::vec3 color     = glm::vec3(1.0f);
    float     intensity = 1.0f;

    // ------------------------------------------------------------
    // Range / attenuation
    // ------------------------------------------------------------
    float range = 0.0f; // 0 = infinite / unused (depends on type)

    // ------------------------------------------------------------
    // Spot parameters
    // ------------------------------------------------------------
    float spotInnerConeRad = 0.0f;
    float spotOuterConeRad = 0.78539816339f; // ~pi/4

    // ------------------------------------------------------------
    // Flags
    // ------------------------------------------------------------
    bool enabled = true;
};
