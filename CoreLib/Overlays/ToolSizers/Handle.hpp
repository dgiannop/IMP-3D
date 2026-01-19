#pragma once

#include <glm/glm.hpp>

class Viewport;
class OverlayHandler;

class Handle
{
public:
    virtual ~Handle() = default;

    virtual void beginDrag(Viewport* vp, float x, float y) = 0;

    virtual void drag(Viewport* vp, float x, float y) = 0;

    virtual void endDrag(Viewport* vp, float x, float y) = 0;

    virtual void construct(Viewport* vp, OverlayHandler& shapeHandler) = 0;

    virtual glm::vec3 position() const = 0;

    virtual glm::ivec3 axis() const = 0;
};
