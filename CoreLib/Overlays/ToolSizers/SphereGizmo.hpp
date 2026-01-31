//=============================================================================
// SphereGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Gizmo for sphere / ellipsoid sizing: center + radius vec3.
 *
 * UX:
 *  - Default (Alt not held): uniform scaling (X/Y/Z radii change together).
 *  - Alt held: per-axis scaling (only the dragged axis changes).
 *
 * Tool contract:
 *  - Tool owns parameters (center + radius vec3).
 *  - Tool forwards input events to this gizmo.
 *  - Gizmo edits tool parameters directly. No tool-side sync helpers.
 *
 * Handles:
 *  - 0: X radius
 *  - 1: Y radius
 *  - 2: Z radius
 *  - 3: Center move (view-plane)
 */
class SphereGizmo final
{
public:
    SphereGizmo(glm::vec3* center, glm::vec3* radius);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

    void                setMinRadius(float v) noexcept { m_minRadius = v; }
    [[nodiscard]] float minRadius() const noexcept { return m_minRadius; }

    [[nodiscard]] bool dragging() const noexcept { return m_dragging; }

private:
    enum class Mode : int32_t
    {
        None   = -1,
        X      = 0,
        Y      = 1,
        Z      = 2,
        Center = 3,
    };

    static Mode modeFromHandle(int32_t h) noexcept
    {
        switch (h)
        {
            case 0:
                return Mode::X;
            case 1:
                return Mode::Y;
            case 2:
                return Mode::Z;
            case 3:
                return Mode::Center;
            default:
                return Mode::None;
        }
    }

    static glm::vec3 axisDir(Mode m) noexcept;
    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept;

    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 const glm::vec3& axis,
                                                 float            mx,
                                                 float            my) const;

    [[nodiscard]] glm::vec3 dragPointOnViewPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 float            mx,
                                                 float            my) const;

    static float clampMin(float v, float minV) noexcept;

    static int axisIndex(Mode m) noexcept;

private:
    glm::vec3* m_center = nullptr;
    glm::vec3* m_radius = nullptr;

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    // Drag state (axis)
    glm::vec3 m_origin     = glm::vec3{0.f};
    glm::vec3 m_axisDir    = glm::vec3{0.f};
    glm::vec3 m_startHit   = glm::vec3{0.f};
    float     m_startParam = 0.f;

    glm::vec3 m_startRadius        = glm::vec3{0.5f};
    float     m_startUniformRadius = 0.5f;

    // Drag state (center)
    glm::vec3 m_startCenter  = glm::vec3{0.f};
    glm::vec3 m_startOnPlane = glm::vec3{0.f};

    // Size tuning (world units derived from pixelScale)
    float m_centerRadiusWorld = 0.05f;
    float m_tipRadiusWorld    = 0.015f;

    float m_minRadius = 0.0001f;
};
