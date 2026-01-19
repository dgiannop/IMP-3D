#pragma once

#include <glm/glm.hpp>

#include "RadiusSizer.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;
class SceneMesh;

/**
 * @class SphereTool
 * @brief Tool for interactively creating spherical geometry.
 *
 * @ingroup MeshTools
 *
 * SphereTool allows the user to define a sphere by dragging in the viewport.
 * Adjustable parameters include radius, center, subdivisions (sides/rings),
 * axis orientation, and smoothing.
 */
class SphereTool final : public Tool
{
public:
    SphereTool();
    ~SphereTool() override = default;

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

    glm::vec3  m_radius{0.f};    ///< Sphere radius along X/Y/Z (supports ellipsoids).
    glm::vec3  m_center{0.f};    ///< World-space sphere center.
    int        m_sides = 24;     ///< Horizontal subdivision count.
    int        m_rings = 12;     ///< Vertical subdivision count.
    glm::ivec3 m_axis{0, 1, 0};  ///< Orientation axis for sphere placement.
    bool       m_smooth = false; ///< Whether normals are smoothed.

    RadiusSizer m_radiusResizer; ///< Helper for interactive radius adjustments.
};
