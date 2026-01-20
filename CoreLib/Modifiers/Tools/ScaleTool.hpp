#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @class ScaleTool
 * @brief Very basic uniform scale tool (WIP). Scales selected verts about selection center.
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
    float m_scale = 1.0f; ///< Uniform scale factor (1 = no-op)

    // Drag state
    float     m_startScale = 1.0f;
    glm::vec3 m_pivot{0.0f};
    int32_t   m_startX = 0;
};
