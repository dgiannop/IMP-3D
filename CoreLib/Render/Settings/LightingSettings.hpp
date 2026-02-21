//============================================================
// LightingSettings.hpp  (FULL REPLACEMENT)
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
    // Scene-light tuning (global multipliers)
    // (Lets you "play" with imported lights without editing assets)
    // --------------------------------------------------------
    float scenePointIntensityMul = 1.0f; // affects LightType::Point only
    float scenePointRangeMul     = 1.0f; // affects LightType::Point only

    float sceneSpotIntensityMul = 1.0f; // affects LightType::Spot only
    float sceneSpotRangeMul     = 1.0f; // affects LightType::Spot only
    float sceneSpotConeMul      = 1.0f; // scales inner+outer cone angles (radians)

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
