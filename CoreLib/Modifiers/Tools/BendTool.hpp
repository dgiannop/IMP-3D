#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @class BendTool
 * @brief Very basic WIP bend tool (axis-based, no gizmo).
 *
 * Bends selected verts around a world axis using a simple arc deformation.
 */
class BendTool final : public Tool
{
public:
    BendTool();
    ~BendTool() = default;

    void activate(Scene* scene) override;
    void propertiesChanged(Scene* scene) override;

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    void            render(Viewport* vp, Scene* scene) override;
    OverlayHandler* overlayHandler() override;

private:
    float      m_angleDeg = 0.0f;      ///< Bend angle in degrees
    float      m_radius   = 0.0f;      ///< If <= 0, auto from bounds
    glm::ivec3 m_axis     = {0, 1, 0}; ///< Bend axis (AXIS property)

    // Drag state
    float   m_startAngleDeg = 0.0f;
    int32_t m_startX        = 0;
};
