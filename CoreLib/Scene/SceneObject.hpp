#pragma once

#include <glm/matrix.hpp>

/**
 * @brief Base class for any object inside a Scene (meshes, lights, cameras).
 *
 * SceneObject provides a minimal interface for scene participation:
 * - per-frame idle/update hooks
 * - transform (model matrix)
 * - visibility
 * - selection
 */
class SceneObject
{
public:
    /** @brief Virtual destructor. */
    virtual ~SceneObject() noexcept = default;

    /**
     * @brief Per-frame idle/update hook.
     * @param scene Owning scene (may be used to query global state)
     */
    virtual void idle(class Scene* scene) = 0;

    // ------------------------------------------------------------
    // Transform
    // ------------------------------------------------------------

    /**
     * @brief Object-to-world transform.
     * @return Model matrix
     */
    [[nodiscard]] virtual glm::mat4 model() const noexcept = 0;

    // ------------------------------------------------------------
    // Visibility
    // ------------------------------------------------------------

    /**
     * @brief Query object visibility.
     * @return True if object should be rendered/considered visible
     */
    [[nodiscard]] virtual bool visible() const noexcept = 0;

    /**
     * @brief Set object visibility.
     * @param value Visibility flag
     */
    virtual void visible(bool value) noexcept = 0;

    // ------------------------------------------------------------
    // Selection
    // ------------------------------------------------------------

    /**
     * @brief Query selection state.
     * @return True if object is selected
     */
    [[nodiscard]] virtual bool selected() const noexcept = 0;

    /**
     * @brief Set selection state.
     * @param value Selection flag
     */
    virtual void selected(bool value) noexcept = 0;
};
