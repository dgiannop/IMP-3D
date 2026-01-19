#pragma once

#include <glm/glm.hpp>

#include "Handle.hpp"

class Viewport;
class OverlayHandler;

class SizeHandle : public Handle
{
public:
    SizeHandle(glm::ivec3 direction, glm::vec3* min, glm::vec3* max);

    virtual void beginDrag(Viewport* vp, float x, float y) override;

    virtual void drag(Viewport* vp, float x, float y) override;

    virtual void endDrag(Viewport* vp, float x, float y) override;

    virtual void construct(Viewport* vp, OverlayHandler& overlayHandler) override;

    virtual glm::vec3 position() const override;

    virtual glm::ivec3 axis() const override;

private:
    glm::ivec3 m_dir;
    glm::vec3* m_min;
    glm::vec3* m_max;

    // Helpers: compute current box center and size from min/max
    glm::vec3 center() const;
    glm::vec3 size() const;
};
