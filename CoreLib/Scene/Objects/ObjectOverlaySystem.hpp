//=============================================================================
// ObjectOverlaySystem.hpp
//=============================================================================
#pragma once

#include "OverlayHandler.hpp"

/**
 * @class ObjectOverlaySystem
 * @brief Scene-owned overlay container for OBJECTS selection mode.
 *
 * Owns an OverlayHandler where systems append overlay primitives for
 * non-mesh scene objects (lights, cameras, etc).
 *
 * Mapping from overlay-id -> object-id is handled by the overlay builders
 * (e.g. SceneLightOverlays) by choosing stable overlay ids.
 */
class ObjectOverlaySystem final
{
public:
    ObjectOverlaySystem()  = default;
    ~ObjectOverlaySystem() = default;

    ObjectOverlaySystem(const ObjectOverlaySystem&)            = delete;
    ObjectOverlaySystem& operator=(const ObjectOverlaySystem&) = delete;

    ObjectOverlaySystem(ObjectOverlaySystem&&) noexcept            = default;
    ObjectOverlaySystem& operator=(ObjectOverlaySystem&&) noexcept = default;

    /** @brief Clear all overlay primitives. */
    void clear() noexcept
    {
        m_overlays.clear();
    }

    /** @brief Access underlying overlay storage. */
    [[nodiscard]] OverlayHandler& overlays() noexcept
    {
        return m_overlays;
    }

    /** @brief Access underlying overlay storage (const). */
    [[nodiscard]] const OverlayHandler& overlays() const noexcept
    {
        return m_overlays;
    }

private:
    OverlayHandler m_overlays = {};
};
