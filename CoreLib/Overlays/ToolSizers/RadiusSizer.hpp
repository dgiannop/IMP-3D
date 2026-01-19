#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "CoreTypes.hpp"
#include "OverlayHandler.hpp"
#include "RadiusHandle.hpp"

class Viewport;
class Scene;

class RadiusSizer
{
public:
    RadiusSizer(glm::vec3* radius, glm::vec3* center);

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
    glm::vec3* m_radius = nullptr;
    glm::vec3* m_center = nullptr;

    int m_curHandle = -1;

    std::vector<RadiusHandle> m_handles;
    OverlayHandler            m_overlayHandler;
};
