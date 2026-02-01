//=============================================================================
// LightOverlaySystem.hpp
//=============================================================================
#pragma once

#include <cstdint>

#include "Light.hpp" // LightId, kInvalidLightId
#include "OverlayHandler.hpp"

class Scene;
class Viewport;

class LightOverlaySystem final
{
public:
    LightOverlaySystem() = default;

    void render(Viewport* vp, Scene* scene);

    /// Returns picked LightId, or kInvalidLightId if none.
    [[nodiscard]] LightId pick(Viewport* vp, float mx, float my) const noexcept;

    OverlayHandler&       overlayHandler() noexcept { return m_overlays; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlays; }

private:
    static constexpr int32_t kLightOverlayBase = 100000;

    static int32_t overlayIdFromLightId(LightId id) noexcept
    {
        return kLightOverlayBase + static_cast<int32_t>(id);
    }

    static bool overlayIdIsLight(int32_t overlayId) noexcept
    {
        return overlayId >= kLightOverlayBase;
    }

    static LightId lightIdFromOverlayId(int32_t overlayId) noexcept
    {
        return static_cast<LightId>(overlayId - kLightOverlayBase);
    }

private:
    void buildPoint(Viewport* vp, const Light& l, float px, const glm::vec4& color);
    void buildDirectional(Viewport* vp, const Light& l, float px, const glm::vec4& color);
    void buildSpot(Viewport* vp, const Light& l, float px, const glm::vec4& color);

    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept;

private:
    OverlayHandler m_overlays = {};
};
