//=============================================================================
// TranslateGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief World-space translate gizmo (XYZ axes + free-move center).
 *
 * This gizmo writes directly into a tool-owned translation parameter (m_amount).
 * Tools typically call propertiesChanged(scene) after mouseDrag updates, so the
 * deformation / rebuild logic stays in the tool rather than in the gizmo.
 *
 * Handles:
 *  - 0: X axis
 *  - 1: Y axis
 *  - 2: Z axis
 *  - 3: Center disk (free move in view plane)
 *
 * Drag model:
 *  - Axis drag: intersects a plane that contains the axis and faces the camera,
 *              then projects delta onto the axis.
 *  - Free drag: intersects the camera-facing plane through the pivot (view plane).
 *
 * The gizmo tracks a "base origin" so the pivot stays stable under absolute
 * parameter dragging:
 *  - curCenter already includes current deformation from *m_amount
 *  - baseOrigin = curCenter - *m_amount
 *  - origin     = baseOrigin + *m_amount
 */
class TranslateGizmo final
{
public:
    explicit TranslateGizmo(glm::vec3* amount);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    /// Build overlays for the current frame (axes + center disk).
    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

private:
    enum class Mode : int32_t
    {
        None = -1,
        X    = 0,
        Y    = 1,
        Z    = 2,
        Free = 3,
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
                return Mode::Free;
            default:
                return Mode::None;
        }
    }

    static glm::vec3 axisDir(Mode m) noexcept;

    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 Mode             axisMode,
                                                 float            mx,
                                                 float            my) const;

    [[nodiscard]] glm::vec3 dragPointOnViewPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 float            mx,
                                                 float            my) const;

    void buildCenterDisk(Viewport*        vp,
                         const glm::vec3& origin,
                         float            radiusWorld,
                         const glm::vec4& color);

private:
    glm::vec3* m_amount = nullptr; ///< Tool-owned translation delta (world space)

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    glm::vec3 m_baseOrigin   = glm::vec3(0.0f); ///< pivot without the current amount applied
    glm::vec3 m_origin       = glm::vec3(0.0f); ///< pivot at drag start (baseOrigin + startAmount)
    glm::vec3 m_axisDir      = glm::vec3(0.0f);
    glm::vec3 m_startOnPlane = glm::vec3(0.0f);
    glm::vec3 m_startAmount  = glm::vec3(0.0f);

    // Pixel-tuned sizes converted to world units at the pivot each frame.
    float m_centerRadiusWorld = 0.05f;
    float m_axisLengthWorld   = 1.0f;
    float m_tipRadiusWorld    = 0.02f;
};
