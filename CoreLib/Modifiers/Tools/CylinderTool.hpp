// CylinderTool.hpp
#pragma once

#include <glm/glm.hpp>

#include "RadiusSizer2D.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;
class SceneMesh;

/**
 * @class CylinderTool
 * @brief Tool for interactively creating solid cylinder geometry.
 *
 * @ingroup MeshTools
 *
 * CylinderTool allows the user to define a cylinder by dragging in the viewport.
 * Adjustable parameters include radius, height, center, subdivisions (sides/segments),
 * axis orientation, and optional caps.
 *
 * UV layout (Primitives::createCylinder):
 *   - Side:       U ∈ [0, 1],     V ∈ [0.0, 0.5]
 *   - Bottom cap: packed left:    U ∈ [0.0, 0.5], V ∈ [0.5, 1.0]
 *   - Top cap:    packed right:   U ∈ [0.5, 1.0], V ∈ [0.5, 1.0]
 */
class CylinderTool final : public Tool
{
public:
    CylinderTool();
    ~CylinderTool() override = default;

    /** @copydoc Tool::activate */
    void activate(Scene* scene) override;

    /** @copydoc Tool::propertiesChanged */
    void propertiesChanged(Scene* scene) override;

    /** @copydoc Tool::mouseDown */
    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    /** @copydoc Tool::mouseDrag */
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    /** @copydoc Tool::mouseUp */
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    /** @copydoc Tool::render */
    void render(Viewport* vp, Scene* scene) override;

    /** @copydoc Tool::overlayHandler */
    OverlayHandler* overlayHandler() override;

private:
    SceneMesh* m_sceneMesh = nullptr; ///< Temporary mesh used during interactive preview.

    float         m_radius = 0.5f; ///< Cylinder radius.
    float         m_height = 1.0f; ///< Cylinder height.
    glm::vec3     m_center{0.f};   ///< World-space cylinder center.
    int           m_sides    = 24; ///< Radial subdivision count.
    int           m_segments = 1;  ///< Height subdivision count.
    glm::ivec3    m_axis{0, 1, 0}; ///< Orientation axis for cylinder placement.
    bool          m_caps = true;   ///< Whether to create caps.
    RadiusSizer2D m_radiusResizer; ///< Helper for interactive radius adjustments.
};
