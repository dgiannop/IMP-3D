#pragma once

#include <glm/glm.hpp>

#include "SelectionUtils.hpp"
#include "Tool.hpp"
#include "TranslateGizmo.hpp"

class Scene;
class Viewport;
class SysMesh;
struct CoreEvent;

/**
 * @class MockTool
 * @brief Interactive tool for testing/debugging.
 *
 * @ingroup MeshTools
 *
 * MockTool provides click-and-drag functionality on selected geometry.
 */
class MockTool final : public Tool
{
public:
    MockTool();
    ~MockTool() = default;

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
    // OverlayHandler* overlayHandler() override;

private:
    glm::vec3 m_amount;

    std::unique_ptr<OverlayHandler> m_overlayHandler;
};
