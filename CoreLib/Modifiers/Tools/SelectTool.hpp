#pragma once

#include "Scene.hpp"
#include "Tool.hpp"

/**
 * @class SelectTool
 * @brief Tool for interactively selecting elements in a Scene.
 *
 * @ingroup MeshTools
 */
class SelectTool final : public Tool
{
public:
    SelectTool();

    ~SelectTool() override = default;

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

private:
    bool m_addMode       = false;
    bool m_selectThrough = false;
};
