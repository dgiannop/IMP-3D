#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @class StretchTool
 * @brief Very basic stretch tool (no gizmo). Non-uniform scale about selection center.
 *
 * Dragging maps X delta -> scale X, Y delta -> scale Y. Z is edited via property.
 */
class StretchTool final : public Tool
{
public:
    StretchTool();
    ~StretchTool() = default;

    void activate(Scene* scene) override;
    void propertiesChanged(Scene* scene) override;

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    void            render(Viewport* vp, Scene* scene) override;
    OverlayHandler* overlayHandler() override;

private:
    glm::vec3 m_scale{1.0f, 1.0f, 1.0f}; ///< Per-axis scale

    // Drag state
    glm::vec3 m_startScale{1.0f, 1.0f, 1.0f};
    glm::vec3 m_pivot{0.0f};
    int32_t   m_startX = 0;
    int32_t   m_startY = 0;
};
