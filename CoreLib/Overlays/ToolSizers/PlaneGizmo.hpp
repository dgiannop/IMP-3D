//=============================================================================
// PlaneGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Gizmo for plane primitives: center + size (width/height) in plane space.
 *
 * Tool contract:
 *  - Tool owns the parameters (center, size, axis).
 *  - Tool forwards input events to this gizmo.
 *  - Gizmo edits tool parameters directly. No tool-side sync helpers.
 *
 * Handles:
 *  - 0: U size (width) along plane U axis
 *  - 1: V size (height) along plane V axis
 *  - 2: Center move (free move in view plane)
 */
class PlaneGizmo final
{
public:
    PlaneGizmo(glm::vec3* center, glm::vec2* size, glm::ivec3* axis);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

    void                setMinSize(float v) noexcept { m_minSize = v; }
    [[nodiscard]] float minSize() const noexcept { return m_minSize; }

    [[nodiscard]] bool dragging() const noexcept { return m_dragging; }

private:
    enum class Mode : int32_t
    {
        None   = -1,
        U      = 0,
        V      = 1,
        Center = 2,
    };

    static Mode modeFromHandle(int32_t h) noexcept
    {
        switch (h)
        {
            case 0:
                return Mode::U;
            case 1:
                return Mode::V;
            case 2:
                return Mode::Center;
            default:
                return Mode::None;
        }
    }

    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept;

    static float clampMin(float v, float minV) noexcept;

    void computePlaneFrame(glm::vec3& outN, glm::vec3& outU, glm::vec3& outV) const noexcept;

    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 const glm::vec3& axisDir,
                                                 float            mx,
                                                 float            my) const;

    [[nodiscard]] glm::vec3 dragPointOnViewPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 float            mx,
                                                 float            my) const;

private:
    glm::vec3*  m_center = nullptr; ///< Tool-owned center (world space)
    glm::vec2*  m_size   = nullptr; ///< Tool-owned size (width/height in plane space)
    glm::ivec3* m_axis   = nullptr; ///< Tool-owned normal axis (major axis)

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    // Drag state (axis handles)
    glm::vec3 m_origin     = glm::vec3{0.f};
    glm::vec3 m_axisDir    = glm::vec3{0.f};
    glm::vec3 m_startHit   = glm::vec3{0.f};
    float     m_startParam = 0.f;

    glm::vec2 m_startSize = glm::vec2{1.f};

    // Drag state (center)
    glm::vec3 m_startCenter  = glm::vec3{0.f};
    glm::vec3 m_startOnPlane = glm::vec3{0.f};

    // Size tuning (world units derived from pixelScale)
    float m_centerRadiusWorld = 0.05f;
    float m_tipRadiusWorld    = 0.015f;

    float m_minSize = 0.0001f;
};
