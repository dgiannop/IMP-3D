#pragma once

#include <glm/mat4x4.hpp>

class Scene;

/**
 * @brief Identifies the concrete category of a SceneObject.
 *
 * SceneObjectType provides a lightweight alternative to RTTI-based type checks.
 * It is intended for fast filtering and dispatch in Scene code (e.g., UI lists,
 * renderer iteration, selection, and outliner views).
 */
enum class SceneObjectType : uint8_t
{
    Mesh,
    Light,
    Camera,
    Empty
};

/**
 * @brief Base class for any object inside a Scene (meshes, lights, cameras).
 *
 * SceneObject provides a minimal interface for scene participation:
 *  - per-frame idle/update hooks
 *  - transform (model matrix)
 *  - visibility
 *  - selection
 *
 * Concrete object category is exposed via type() to avoid repeated dynamic_cast
 * usage when filtering Scene objects.
 */
class SceneObject
{
public:
    /** @brief Virtual destructor. */
    virtual ~SceneObject() noexcept = default;

    /**
     * @brief Returns the object category.
     *
     * This is used for fast filtering and safe downcasting after a type() check.
     */
    [[nodiscard]] virtual SceneObjectType type() const noexcept = 0;

    /**
     * @brief Per-frame idle/update hook.
     * @param scene Owning scene (may be used to query global state).
     */
    virtual void idle(Scene* scene) = 0;

    // ------------------------------------------------------------
    // Transform
    // ------------------------------------------------------------

    /**
     * @brief Object-to-world transform.
     * @return Model matrix.
     */
    [[nodiscard]] virtual glm::mat4 model() const noexcept = 0;

    // ------------------------------------------------------------
    // Visibility
    // ------------------------------------------------------------

    /**
     * @brief Queries object visibility.
     * @return True if the object should be rendered/considered visible.
     */
    [[nodiscard]] virtual bool visible() const noexcept = 0;

    /**
     * @brief Sets object visibility.
     * @param value Visibility flag.
     */
    virtual void visible(bool value) noexcept = 0;

    // ------------------------------------------------------------
    // Selection
    // ------------------------------------------------------------

    /**
     * @brief Queries selection state.
     * @return True if the object is selected.
     */
    [[nodiscard]] virtual bool selected() const noexcept = 0;

    /**
     * @brief Sets selection state.
     * @param value Selection flag.
     */
    virtual void selected(bool value) noexcept = 0;
};
