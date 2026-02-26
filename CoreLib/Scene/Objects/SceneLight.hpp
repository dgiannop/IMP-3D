//============================================================
// SceneLight.hpp  (FULL REPLACEMENT)
//============================================================
#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <string_view>

#include "Light.hpp"
#include "SceneObject.hpp"

class LightHandler;
class Scene;

/**
 * @brief Scene object that references a Light owned by LightHandler.
 *
 * SceneLight provides SceneObject behavior (transform, visibility, selection)
 * while delegating light parameter storage (color, intensity, type, etc.) to
 * LightHandler via a stable LightId.
 *
 * This design avoids duplicating light parameters in multiple locations and
 * enables future scene graph features (parenting, instancing, hierarchy UI)
 * without changing the underlying light storage model.
 */
class SceneLight final : public SceneObject
{
public:
    /**
     * @brief Constructs a SceneLight that references an existing Light.
     * @param lightHandler Owning light storage (non-owning pointer).
     * @param id Stable light identifier in the LightHandler.
     * @param name Display name for the scene object (not required to match Light::name).
     */
    explicit SceneLight(LightHandler* lightHandler, LightId id, std::string_view name);

    /** @brief Destroys the SceneLight. */
    ~SceneLight() override = default;

    SceneLight(const SceneLight&)            = delete;
    SceneLight& operator=(const SceneLight&) = delete;

    SceneLight(SceneLight&&) noexcept            = default;
    SceneLight& operator=(SceneLight&&) noexcept = default;

    /** @copydoc SceneObject::type */
    [[nodiscard]] SceneObjectType type() const noexcept override { return SceneObjectType::Light; }

    /** @copydoc SceneObject::idle */
    void idle(Scene* scene) override;

    /** @copydoc SceneObject::model */
    [[nodiscard]] glm::mat4 model() const noexcept override;

    /**
     * @brief Sets the object-to-world transform.
     * @param mtx Model matrix.
     */
    void model(const glm::mat4& mtx) noexcept;

    /** @copydoc SceneObject::visible */
    [[nodiscard]] bool visible() const noexcept override;

    /** @copydoc SceneObject::visible(bool) */
    void visible(bool value) noexcept override;

    /** @copydoc SceneObject::selected */
    [[nodiscard]] bool selected() const noexcept override;

    /** @copydoc SceneObject::selected(bool) */
    void selected(bool value) noexcept override;

    /**
     * @brief Returns the referenced LightId.
     * @return Stable LightId.
     */
    [[nodiscard]] LightId lightId() const noexcept;

    /**
     * @brief Returns the scene object display name.
     * @return Name view (lifetime owned by this object).
     */
    [[nodiscard]] std::string_view name() const noexcept;

    /**
     * @brief Sets the scene object display name AND updates the underlying Light::name.
     *
     * This keeps the SceneLight label and the authoritative Light data in sync.
     */
    void name(std::string_view n) noexcept;

    // ------------------------------------------------------------
    // Delegated Light parameters (read/write underlying Light)
    // ------------------------------------------------------------
    [[nodiscard]] bool enabled() const noexcept;
    void               enabled(bool v) noexcept;

    [[nodiscard]] LightType lightType() const noexcept;
    void                    lightType(LightType t) noexcept;

    [[nodiscard]] glm::vec3 color() const noexcept;
    void                    color(const glm::vec3& c) noexcept;

    [[nodiscard]] float intensity() const noexcept;
    void                intensity(float v) noexcept;

    [[nodiscard]] float range() const noexcept;
    void                range(float v) noexcept;

    [[nodiscard]] float spotInnerConeRad() const noexcept;
    void                spotInnerConeRad(float r) noexcept;

    [[nodiscard]] float spotOuterConeRad() const noexcept;
    void                spotOuterConeRad(float r) noexcept;

    [[nodiscard]] bool affectRaster() const noexcept;
    void               affectRaster(bool v) noexcept;

    [[nodiscard]] bool affectRt() const noexcept;
    void               affectRt(bool v) noexcept;

    [[nodiscard]] bool castShadows() const noexcept;
    void               castShadows(bool v) noexcept;

    // Convenience accessors (WORLD space) - useful for gizmos.
    [[nodiscard]] glm::vec3 position() const noexcept;
    void                    position(const glm::vec3& p) noexcept;

    [[nodiscard]] glm::vec3 direction() const noexcept;
    void                    direction(const glm::vec3& d) noexcept;

private:
    [[nodiscard]] Light*       lightPtr() noexcept;
    [[nodiscard]] const Light* lightPtr() const noexcept;

    void notifyChanged() noexcept;

private:
    /** @brief Non-owning pointer to the owning light storage. */
    LightHandler* m_lightHandler = nullptr;

    /** @brief Stable identifier for the referenced Light. */
    LightId m_lightId = kInvalidLightId;

    /** @brief Object-to-world transform. */
    glm::mat4 m_model = glm::mat4{1.f};

    /** @brief Visibility flag. */
    bool m_visible = true;

    /** @brief Selection flag. */
    bool m_selected = false;

    /** @brief Scene object display name storage. */
    std::string m_name;
};
