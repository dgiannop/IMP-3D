//============================================================
// LightingSettings.hpp
//============================================================
#pragma once

#include <cstdint>

struct LightingSettings
{
    // --------------------------------------------------------
    // Sources (global switches)
    // --------------------------------------------------------
    bool useHeadlight   = true;
    bool useSceneLights = true;

    float headlightIntensity = 1.0f;
    float ambientFill        = 0.10f;

    // --------------------------------------------------------
    // Exposure / tonemap
    // --------------------------------------------------------
    float exposure = 1.0f;
    bool  tonemap  = true;

    // --------------------------------------------------------
    // Mode policy
    // --------------------------------------------------------
    enum class ModePolicy : uint8_t
    {
        HeadlightOnly = 0,
        SceneOnly     = 1,
        Both          = 2
    };

    ModePolicy solidPolicy  = ModePolicy::HeadlightOnly;
    ModePolicy shadedPolicy = ModePolicy::Both;
    ModePolicy rtPolicy     = ModePolicy::Both;

    // --------------------------------------------------------
    // Debug
    // --------------------------------------------------------
    bool  clampRadiance = true;
    float clampMax      = 10.0f;
};
