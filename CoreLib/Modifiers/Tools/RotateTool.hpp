#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @class RotateTool
 * @brief Very basic rotate tool (WIP). Rotates selected verts around selection center.
 *
 * Dragging adjusts Yaw by default (screen X delta). You can also edit Pitch/Roll as properties.
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
    glm::vec3 m_anglesDeg{0.0f}; ///< Pitch(X), Yaw(Y), Roll(Z) in degrees

    // Drag state
    glm::vec3 m_startAnglesDeg{0.0f};
    glm::vec3 m_pivot{0.0f};
    int32_t   m_startX = 0;
    int32_t   m_startY = 0;
};
