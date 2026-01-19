#pragma once

#include <glm/glm.hpp>
#include <vector>

#include "CoreTypes.hpp"
#include "OverlayHandler.hpp"
#include "RadiusHandle2D.hpp"

class Viewport;
class Scene;

/**
 * @brief Two-handle sizer for cylinder-like primitives (radius + height).
 *
 * Handles:
 *  - 0: radius handle (perpendicular to axis)
 *  - 1: height handle (along axis)
 *  - 2: center handle (optional)
 */
class RadiusSizer2D
{
public:
    RadiusSizer2D(float* radius, float* height, glm::vec3* center, glm::ivec3* axis);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler& overlayHandler()
    {
        return m_overlayHandler;
    }
    const OverlayHandler& overlayHandler() const
    {
        return m_overlayHandler;
    }

private:
    float*      m_radius = nullptr;
    float*      m_height = nullptr;
    glm::vec3*  m_center = nullptr;
    glm::ivec3* m_axis   = nullptr;

    int m_curHandle = -1;

    std::vector<RadiusHandle2D> m_handles;
    OverlayHandler              m_overlayHandler;
};
