//=============================================================================
// ExtentsGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief World-axis extents gizmo (XYZ radius/extents + free-move center).
 *
 * Drives:
 *  - center:  translation (free move in view plane)
 *  - extents: per-axis half-extents / radii (X/Y/Z)
 *
 * This gizmo is intentionally primitive-agnostic and can be reused for:
 *  - spheres / ellipsoids (extents == radius vec3)
 *  - cylinders (tool maps extents.xz -> radius, extents.y -> half-height)
 *  - cones / capsules / tori (tool maps extents to semantic parameters)
 *
 * Handles:
 *  - 0: X extent
 *  - 1: Y extent
 *  - 2: Z extent
 *  - 3: Center move (free move in view plane)
 */
class ExtentsGizmo final
{
public:
    ExtentsGizmo(glm::vec3* center, glm::vec3* extents);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

    void                setMinExtent(float v) noexcept { m_minExtent = v; }
    [[nodiscard]] float minExtent() const noexcept { return m_minExtent; }

    /// True while a handle is actively being dragged.
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
                                                 Mode             axisMode,
                                                 float            mx,
                                                 float            my) const;

    [[nodiscard]] glm::vec3 dragPointOnViewPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 float            mx,
                                                 float            my) const;

    static float clampExtent(float v, float minV) noexcept;

private:
    glm::vec3* m_center  = nullptr; ///< Tool-owned primitive center (world space)
    glm::vec3* m_extents = nullptr; ///< Tool-owned extents/radii (world space)

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    // Drag state (axis)
    glm::vec3 m_origin       = glm::vec3{0.0f};
    glm::vec3 m_startExtents = glm::vec3{0.5f};
    glm::vec3 m_axisDir      = glm::vec3{0.0f};

    glm::vec3 m_startHit   = glm::vec3{0.0f};
    float     m_startParam = 0.0f;

    // Drag state (center)
    glm::vec3 m_startCenter  = glm::vec3{0.0f};
    glm::vec3 m_startOnPlane = glm::vec3{0.0f};

    // Size tuning (world units at pivot, derived from pixelScale)
    float m_centerRadiusWorld = 0.05f;
    float m_tipRadiusWorld    = 0.015f;

    float m_minExtent = 0.0001f;
};
