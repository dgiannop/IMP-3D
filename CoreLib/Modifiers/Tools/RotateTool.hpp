#pragma once

#include <glm/glm.hpp>

#include "RotateGizmo.hpp"
#include "SelectionUtils.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @class RotateTool
 * @brief Interactive rotation tool using a world-axis rotate gizmo.
 *
 * This tool performs an interactive, preview-based rotation of the current
 * selection around its center using a RotateGizmo.
 *
 * Design notes:
 *  - Rotation is applied as a *delta* (Euler angles in degrees).
 *  - During interaction, geometry is previewed by aborting and reapplying
 *    mesh changes on each update.
 *  - On mouse release, the preview is committed and the delta is reset.
 *  - The gizmo owns all overlay rendering and picking logic; the tool only
 *    applies the resulting parameter changes.
 *
 * This mirrors the interaction model used by TranslateTool and other
 * core transform tools.
 */
class RotateTool final : public Tool
{
public:
    RotateTool();
    ~RotateTool() = default;

    void activate(Scene* scene) override;
    void propertiesChanged(Scene* scene) override;

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    void            render(Viewport* vp, Scene* scene) override;
    OverlayHandler* overlayHandler() override;

private:
    glm::vec3 m_anglesDeg{0.0f}; ///< Pitch(X), Yaw(Y), Roll(Z) in degrees (delta while dragging)

    RotateGizmo m_gizmo{&m_anglesDeg};
};
