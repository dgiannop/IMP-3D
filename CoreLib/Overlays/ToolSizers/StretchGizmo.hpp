//=============================================================================
// StretchGizmo.hpp
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
 * @brief World-axis stretch gizmo (uniform + per-axis).
 *
 * Writes scale factors into a tool-owned parameter:
 *  - (1,1,1) is a no-op
 *  - (sx,sy,sz) scales about the current selection pivot
 *
 * Handles:
 *  - 0: X axis stretch
 *  - 1: Y axis stretch
 *  - 2: Z axis stretch
 *  - 3: Center (uniform stretch)
 */
class StretchGizmo final
{
public:
    explicit StretchGizmo(glm::vec3* scale);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

private:
    enum class Mode : int32_t
    {
        None    = -1,
        X       = 0,
        Y       = 1,
        Z       = 2,
        Uniform = 3,
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
                return Mode::Uniform;
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

    void buildBillboardSquare(Viewport*        vp,
                              const glm::vec3& center,
                              float            halfExtentWorld,
                              const glm::vec4& color,
                              bool             filledForPick);

private:
    glm::vec3* m_scale = nullptr; ///< Tool-owned scale factors (1=no-op)

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    glm::vec3 m_baseOrigin = glm::vec3{0.0f}; ///< pivot without current preview
    glm::vec3 m_origin     = glm::vec3{0.0f}; ///< pivot with current preview

    glm::vec3 m_startScale = glm::vec3{1.0f};
    glm::vec3 m_axisDir    = glm::vec3{0.0f};

    // Axis drag anchors
    glm::vec3 m_startHit   = glm::vec3{0.0f};
    float     m_startParam = 1.0f;

    // Uniform drag anchor (screen space)
    float m_startMx = 0.0f;
    float m_startMy = 0.0f;

    // Size tuning (world units at pivot, derived from pixelScale)
    float m_centerHalfWorld  = 0.02f;
    float m_axisLenWorld     = 0.2f;
    float m_axisBoxHalfWorld = 0.015f;
};
