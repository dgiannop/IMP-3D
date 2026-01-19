#pragma once

#include "BoxSizer.hpp"
#include "Scene.hpp"
#include "Tool.hpp"

/**
 * @class BoxTool
 * @brief Tool for interactively creating and editing box primitives in a Scene.
 *
 * @ingroup MeshTools
 */
class BoxTool final : public Tool
{
public:
    BoxTool();
    ~BoxTool() override = default;

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
    class SceneMesh* m_sceneMesh = nullptr;

    glm::vec3  m_size;
    glm::vec3  m_center;
    glm::ivec3 m_segs;

    // float m_radius    = 0.f;
    // int   m_roundSegs = 4;

    BoxSizer m_boxSizer;
};
