#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "StretchGizmo.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @class StretchTool
 * @brief Stretch tool with gizmo. Non-uniform scale about selection center.
 *
 * Gizmo outputs m_scale:
 *  - uniform via center handle
 *  - per-axis via X/Y/Z handles
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

    StretchGizmo m_gizmo{&m_scale};
};
