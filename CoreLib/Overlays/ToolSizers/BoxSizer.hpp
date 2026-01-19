#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "OverlayHandler.hpp"
#include "Scene.hpp"
#include "SizeHandle.hpp"
#include "Viewport.hpp"

class BoxSizer
{
public:
    BoxSizer(glm::vec3* size, glm::vec3* center);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    /// Build overlays for the current frame (handles).
    void render(Viewport* vp, Scene* scene);

    /// Access the internal overlay handler (for Renderer).
    OverlayHandler& overlayHandler()
    {
        return m_overlayHandler;
    }

    const OverlayHandler& overlayHandler() const
    {
        return m_overlayHandler;
    }

private:
    glm::vec3  m_min, m_max;
    glm::vec3* m_size;
    glm::vec3* m_center;
    int        m_curHandle = -1;

    std::vector<SizeHandle> m_handles;
    OverlayHandler          m_overlayHandler;
};
