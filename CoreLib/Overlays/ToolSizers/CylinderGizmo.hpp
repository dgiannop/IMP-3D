//=============================================================================
// CylinderGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Gizmo for cylinder-like primitives: radius + height + center.
 *
 * Tool contract:
 *  - Tool owns the parameters (radius/height/center).
 *  - Tool forwards events to this gizmo.
 *  - Gizmo edits tool parameters directly. No tool-side sync helpers.
 *
 * Handles:
 *  - 0: Radius (X)
 *  - 1: Half-height (Y)
 *  - 2: Radius (Z)
 *  - 3: Center move (view-plane)
 */
class CylinderGizmo final
{
public:
    CylinderGizmo(glm::vec3* center, float* radius, float* height);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

    void                setMinRadius(float v) noexcept { m_minRadius = v; }
    void                setMinHeight(float v) noexcept { m_minHeight = v; }
    [[nodiscard]] float minRadius() const noexcept { return m_minRadius; }
    [[nodiscard]] float minHeight() const noexcept { return m_minHeight; }

    [[nodiscard]] bool dragging() const noexcept { return m_dragging; }

private:
    enum class Mode : int32_t
    {
        None   = -1,
        RadX   = 0,
        HalfY  = 1,
        RadZ   = 2,
        Center = 3,
    };

    static Mode modeFromHandle(int32_t h) noexcept
    {
        switch (h)
        {
            case 0:
                return Mode::RadX;
            case 1:
                return Mode::HalfY;
            case 2:
                return Mode::RadZ;
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

private:
    glm::vec3* m_center = nullptr;
    float*     m_radius = nullptr;
    float*     m_height = nullptr;

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    // Drag state
    glm::vec3 m_origin     = glm::vec3{0.f};
    glm::vec3 m_axisDir    = glm::vec3{0.f};
    glm::vec3 m_startHit   = glm::vec3{0.f};
    float     m_startParam = 0.f;

    float m_startRadius = 0.5f;
    float m_startHeight = 1.0f;

    // Center drag
    glm::vec3 m_startCenter  = glm::vec3{0.f};
    glm::vec3 m_startOnPlane = glm::vec3{0.f};

    // Size tuning (world units derived from pixelScale)
    float m_centerRadiusWorld = 0.05f;
    float m_tipRadiusWorld    = 0.015f;

    float m_minRadius = 0.0001f;
    float m_minHeight = 0.0001f;
};
