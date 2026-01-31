//=============================================================================
// NormalPullGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Single-handle gizmo that drags a scalar amount along selection normal.
 *
 * Handle:
 *  - 0: the only handle
 *
 * Tool contract:
 *  - mouseDown/Drag/Up forward to the gizmo
 *  - tool calls propertiesChanged(scene) after Drag to rebuild preview
 *  - on mouseUp tool commits and resets amount back to 0
 */
class NormalPullGizmo final
{
public:
    explicit NormalPullGizmo(float* amount);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

private:
    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 const glm::vec3& axisDir,
                                                 float            mx,
                                                 float            my) const;

    void buildBillboardSquare(Viewport*        vp,
                              const glm::vec3& center,
                              float            halfExtentWorld,
                              const glm::vec4& color,
                              bool             filledForPick);

    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept;

private:
    float* m_amount = nullptr; ///< Tool-owned delta (0=no-op)

    OverlayHandler m_overlayHandler = {};

    bool m_dragging = false;

    glm::vec3 m_origin = glm::vec3{0.0f};             ///< pivot
    glm::vec3 m_axis   = glm::vec3{0.0f, 0.0f, 1.0f}; ///< selection normal (world)

    float m_startAmount = 0.0f;

    glm::vec3 m_startHit   = glm::vec3{0.0f};
    float     m_startParam = 0.0f; // dot(hit-origin, axis)

    // Sizing (world units at pivot, derived from pixelScale)
    float m_axisLenWorld = 0.2f;
    float m_tipHalfWorld = 0.015f;
};
