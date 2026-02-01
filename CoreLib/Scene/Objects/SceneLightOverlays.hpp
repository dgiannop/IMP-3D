//=============================================================================
// SceneLightOverlays.hpp
//=============================================================================
#pragma once

#include "Light.hpp" // LightId

class Scene;
class Viewport;
class ObjectOverlaySystem;

namespace scene_overlays
{
    /**
     * @brief Append light overlays into the object overlay system.
     *
     * Builds overlays for all enabled lights in the scene using stable overlay ids.
     * Overlay ids encode the LightId, so pick results can be converted back.
     */
    void appendLights(Viewport* vp, Scene* scene, ObjectOverlaySystem& out);

    /**
     * @brief Convert LightId -> overlay id used by the overlays.
     */
    [[nodiscard]] int32_t overlayIdFromLightId(LightId id) noexcept;

    /**
     * @brief Test if an overlay id belongs to a light overlay.
     */
    [[nodiscard]] bool overlayIdIsLight(int32_t overlayId) noexcept;

    /**
     * @brief Convert overlay id -> LightId.
     *
     * Caller must ensure overlayIdIsLight(overlayId) is true.
     */
    [[nodiscard]] LightId lightIdFromOverlayId(int32_t overlayId) noexcept;

} // namespace scene_overlays
