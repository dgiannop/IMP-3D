#pragma once

#include <glm/matrix.hpp>
/**
 * @brief Base class for any object inside a Scene (meshes, lights, cameras).
 */
class SceneObject
{
public:
    virtual ~SceneObject() noexcept = default;

    virtual void idle(class Scene* scene) = 0;

    // Transform
    [[nodiscard]]
    virtual glm::mat4 model() const noexcept = 0;

    // Visibility
    [[nodiscard]]
    virtual bool visible() const noexcept    = 0;
    virtual void visible(bool value) noexcept = 0;

    // Selection
    [[nodiscard]]
    virtual bool selected() const noexcept    = 0;
    virtual void selected(bool value) noexcept = 0;
};

