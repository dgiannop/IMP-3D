#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Single-handle gizmo that drags along an axis (typically selection normal).
 *
 * Two render behaviors:
 *  - FollowAmountBase = true  : base moves with amount (extrude-like)
 *  - FollowAmountBase = false : base stays fixed; stem length changes (bevel-like)
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

    void               setFollowAmountBase(bool v) noexcept { m_followAmountBase = v; }
    [[nodiscard]] bool followAmountBase() const noexcept { return m_followAmountBase; }

private:
    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 const glm::vec3& axisDir,
                                                 float            mx,
                                                 float            my) const;

private:
    float* m_amount = nullptr; ///< Tool-owned scalar delta (0 = no-op)

    OverlayHandler m_overlayHandler = {};

    bool      m_dragging         = false;
    bool      m_followAmountBase = true;
    glm::vec3 m_origin           = glm::vec3{0.0f};
    glm::vec3 m_axis             = glm::vec3{0.0f, 0.0f, 1.0f};

    float     m_startAmount = 0.0f;
    glm::vec3 m_startHit    = glm::vec3{0.0f};
    float     m_startParam  = 0.0f;

    // Size tuning (world units at pivot, derived from pixelScale).
    float m_axisLenWorld   = 0.2f;
    float m_tipRadiusWorld = 0.015f;
};
