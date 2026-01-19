#pragma once

#include <glm/glm.hpp>

#include "Handle.hpp"

class Viewport;
class OverlayHandler;

class RadiusHandle : public Handle
{
public:
    RadiusHandle(glm::ivec3 direction, glm::vec3* radius, glm::vec3* center);

    virtual void beginDrag(Viewport* vp, float x, float y) override;

    virtual void drag(Viewport* vp, float x, float y) override;

    virtual void endDrag(Viewport* vp, float x, float y) override;

    virtual void construct(Viewport* vp, OverlayHandler& overlayHandler) override;

    virtual glm::vec3 position() const override;

    virtual glm::ivec3 axis() const override;

private:
    glm::ivec3 m_dir;
    glm::vec3* m_radius;
    glm::vec3* m_center;
};
