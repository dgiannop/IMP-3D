#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "Tool.hpp"
#include "TranslateGizmo.hpp"

class Scene;
class Viewport;
class SysMesh;
struct CoreEvent;

// #define use_test_path
/**
 * @class MoveTool
 * @brief Interactive tool for moving selected elements in the scene.
 *
 * @ingroup MeshTools
 *
 * MoveTool provides click-and-drag translation of selected geometry,
 * typically using viewport-space mouse deltas to compute world-space motion.
 */
class MoveTool final : public Tool
{
public:
    MoveTool();
    ~MoveTool() = default;

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
#ifdef use_test_path
    glm::vec3 m_amount = {};

    glm::vec3 m_startAmount = {};

    glm::vec3 m_pivot    = {}; // selection pivot stabilized against startAmount
    glm::vec3 m_startHit = {}; // initial ray-plane hit point
    glm::vec3 m_planeN   = {}; // drag plane normal (constant during drag)
#else
    glm::vec3 m_amount{0.0f}; ///< Current world-space delta

    TranslateGizmo m_gizmo; ///< XYZ axis gizmo
#endif
};
