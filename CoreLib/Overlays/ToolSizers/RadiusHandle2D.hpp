#pragma once

#include <glm/glm.hpp>

#include "Handle.hpp"

class Viewport;
class OverlayHandler;

class RadiusHandle2D : public Handle
{
public:
    RadiusHandle2D(glm::ivec3 direction, float* radius, float* height, glm::vec3* center, glm::ivec3* axis);

    void beginDrag(Viewport* vp, float x, float y) override;

    void drag(Viewport* vp, float x, float y) override;

    void endDrag(Viewport* vp, float x, float y) override;

    void construct(Viewport* vp, OverlayHandler& overlayHandler) override;

    glm::vec3 position() const override;

    glm::ivec3 axis() const override;

private:
    glm::ivec3  m_dir{};
    float*      m_radius = nullptr;
    float*      m_height = nullptr;
    glm::vec3*  m_center = nullptr;
    glm::ivec3* m_axis   = nullptr;
};

// later want “Blender-quality” constraints, we can switch the drag math to use vp->rayPlaneHit() / vp->rayLineClosest() style methods
