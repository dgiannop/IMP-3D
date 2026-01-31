#pragma once

#include <glm/glm.hpp>

#include "ScaleGizmo.hpp"
#include "SelectionUtils.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Uniform scale tool (preview-based).
 *
 * The tool previews changes by aborting the previous preview and re-applying the
 * current scale delta. The delta resets to 1.0 after commit.
 */
class ScaleTool final : public Tool
{
public:
    ScaleTool();
    ~ScaleTool() = default;

    void activate(Scene* scene) override;
    void propertiesChanged(Scene* scene) override;

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    void            render(Viewport* vp, Scene* scene) override;
    OverlayHandler* overlayHandler() override;

private:
    float     m_uniformScale = 1.0f;            ///< UI-facing scalar (1=no-op)
    glm::vec3 m_scale        = glm::vec3{1.0f}; ///< Gizmo-facing scale factors

    ScaleGizmo m_gizmo{&m_scale};
};
