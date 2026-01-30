#pragma once

#include <glm/mat4x4.hpp>
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

// #pragma once

// #include <cstdint>
// #include <glm/glm.hpp>
// #include <glm/matrix.hpp>

// #include "SceneObject.hpp"

// /**
//  * @brief Scene light object (directional / point / spot).
//  *
//  * SceneLight is a SceneObject so it can live in the Scene graph and later
//  * be selectable, transformable, and editable.
//  */
// class SceneLight final : public SceneObject
// {
// public:
//     enum class Type : uint32_t
//     {
//         DIRECTIONAL = 0,
//         POINT       = 1,
//         SPOT        = 2,
//     };

//     SceneLight() = default;
//     explicit SceneLight(Type type) : m_type(type)
//     {
//     }
//     ~SceneLight() noexcept override = default;

//     // ------------------------------------------------------------
//     // SceneObject
//     // ------------------------------------------------------------

//     void idle(Scene* /*scene*/) override
//     {
//     }

//     [[nodiscard]] glm::mat4 model() const noexcept override
//     {
//         return m_model;
//     }

//     [[nodiscard]] bool visible() const noexcept override
//     {
//         return m_visible;
//     }
//     void visible(bool value) noexcept override
//     {
//         m_visible = value;
//     }

//     [[nodiscard]] bool selected() const noexcept override
//     {
//         return m_selected;
//     }
//     void selected(bool value) noexcept override
//     {
//         m_selected = value;
//     }

//     // ------------------------------------------------------------
//     // Light properties
//     // ------------------------------------------------------------

//     [[nodiscard]] Type type() const noexcept
//     {
//         return m_type;
//     }
//     void type(Type t) noexcept
//     {
//         m_type = t;
//     }

//     [[nodiscard]] glm::vec3 color() const noexcept
//     {
//         return m_color;
//     }
//     void color(const glm::vec3& c) noexcept
//     {
//         m_color = c;
//     }

//     [[nodiscard]] float intensity() const noexcept
//     {
//         return m_intensity;
//     }
//     void intensity(float v) noexcept
//     {
//         m_intensity = v;
//     }

//     // Directional “sun” uses direction (unit vector).
//     // Point/spot use position from model() translation.
//     //
//     // We can choose either:
//     //  A) author direction explicitly (m_direction), or
//     //  B) derive direction from model() orientation.
//     //
//     // For now keep it explicit to match your current workflow.

//     [[nodiscard]] glm::vec3 direction() const noexcept
//     {
//         return m_direction;
//     }
//     void direction(const glm::vec3& d) noexcept
//     {
//         m_direction = d;
//     }

//     [[nodiscard]] float range() const noexcept
//     {
//         return m_range;
//     }
//     void range(float r) noexcept
//     {
//         m_range = r;
//     }

//     [[nodiscard]] float spotInnerAngleRad() const noexcept
//     {
//         return m_spotInnerAngleRad;
//     }
//     [[nodiscard]] float spotOuterAngleRad() const noexcept
//     {
//         return m_spotOuterAngleRad;
//     }
//     void spotAnglesRad(float innerRad, float outerRad) noexcept
//     {
//         m_spotInnerAngleRad = innerRad;
//         m_spotOuterAngleRad = outerRad;
//     }

//     // Transform setter for later (gizmo etc.)
//     void model(const glm::mat4& m) noexcept
//     {
//         m_model = m;
//     }

//     // Helpers
//     [[nodiscard]] glm::vec3 position() const noexcept
//     {
//         return glm::vec3(m_model[3]); // translation
//     }

// private:
//     Type      m_type  = Type::DIRECTIONAL;
//     glm::mat4 m_model = glm::mat4(1.0f);

//     bool m_visible  = true;
//     bool m_selected = false;

//     glm::vec3 m_color     = glm::vec3(1.0f);
//     float     m_intensity = 2.0f;

//     // Directional / Spot
//     glm::vec3 m_direction = glm::normalize(glm::vec3(0.3f, 0.7f, 0.2f));

//     // Point / Spot attenuation range (editor friendly)
//     float m_range = 25.0f;

//     // Spot
//     float m_spotInnerAngleRad = 0.35f;
//     float m_spotOuterAngleRad = 0.55f;
// };
