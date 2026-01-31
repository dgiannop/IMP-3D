//=============================================================================
// PlaneTool.hpp
//=============================================================================
#pragma once

#include <glm/glm.hpp>

#include "PlaneGizmo.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;
class SceneMesh;

/**
 * @class PlaneTool
 * @brief Tool for interactively creating and editing plane primitives in a Scene.
 *
 * @ingroup MeshTools
 */
class PlaneTool final : public Tool
{
public:
    PlaneTool();
    ~PlaneTool() override = default;

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

    glm::vec2  m_size{1.f, 1.f}; ///< Plane size (width/height) in plane space.
    glm::vec3  m_center{0.f};    ///< World-space plane center.
    glm::ivec2 m_segs{1, 1};     ///< Subdivision counts (U/V).
    glm::ivec3 m_axis{0, 1, 0};  ///< Plane normal axis (major axis).

    PlaneGizmo m_gizmo;
};
