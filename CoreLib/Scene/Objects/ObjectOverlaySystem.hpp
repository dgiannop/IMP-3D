//=============================================================================
// ObjectOverlaySystem.hpp
//=============================================================================
#pragma once

#include "OverlayHandler.hpp"

/**
 * @brief Scene-owned overlay container for non-mesh scene objects.
 *
 * This is the generic scene overlay system used for OBJECTS mode selection
 * visualization (lights today, cameras/empties later).
 *
 * It intentionally wraps OverlayHandler rather than inheriting from it:
 *  - keeps responsibilities clear (this is "scene object overlays")
 *  - allows future per-object metadata/caching without touching OverlayHandler
 */
class ObjectOverlaySystem final
{
public:
    ObjectOverlaySystem()  = default;
    ~ObjectOverlaySystem() = default;

    ObjectOverlaySystem(const ObjectOverlaySystem&)            = default;
    ObjectOverlaySystem& operator=(const ObjectOverlaySystem&) = default;

    ObjectOverlaySystem(ObjectOverlaySystem&&) noexcept            = default;
    ObjectOverlaySystem& operator=(ObjectOverlaySystem&&) noexcept = default;

    /** @brief Clears all accumulated overlays. */
    void clear() noexcept
    {
        m_overlays.clear();
    }

    [[nodiscard]] OverlayHandler& handler() noexcept
    {
        return m_overlays;
    }

    [[nodiscard]] const OverlayHandler& handler() const noexcept
    {
        return m_overlays;
    }

    /** @brief Direct access to the underlying overlay handler (mutable). */
    [[nodiscard]] OverlayHandler& overlays() noexcept
    {
        return m_overlays;
    }

    /** @brief Direct access to the underlying overlay handler (const). */
    [[nodiscard]] const OverlayHandler& overlays() const noexcept
    {
        return m_overlays;
    }

    // ------------------------------------------------------------
    // Convenience conversions
    // ------------------------------------------------------------
    //
    // These make it painless to pass ObjectOverlaySystem anywhere a
    // const OverlayHandler& is expected (e.g., Renderer::drawOverlays).
    //

    [[nodiscard]] operator OverlayHandler&() noexcept
    {
        return m_overlays;
    }

    [[nodiscard]] operator const OverlayHandler&() const noexcept
    {
        return m_overlays;
    }

private:
    OverlayHandler m_overlays = {};
};
