#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/matrix.hpp>

#include "SceneObject.hpp"

/**
 * @brief Scene light object (directional / point / spot).
 *
 * SceneLight is a SceneObject so it can live in the Scene graph and later
 * be selectable, transformable, and editable.
 */
class SceneLight final : public SceneObject
{
public:
    enum class Type : uint32_t
    {
        DIRECTIONAL = 0,
        POINT       = 1,
        SPOT        = 2,
    };

    SceneLight() = default;
    explicit SceneLight(Type type) : m_type(type)
    {
    }
    ~SceneLight() noexcept override = default;

    // ------------------------------------------------------------
    // SceneObject
    // ------------------------------------------------------------

    void idle(Scene* /*scene*/) override
    {
    }

    [[nodiscard]] glm::mat4 model() const noexcept override
    {
        return m_model;
    }

    [[nodiscard]] bool visible() const noexcept override
    {
        return m_visible;
    }
    void visible(bool value) noexcept override
    {
        m_visible = value;
    }

    [[nodiscard]] bool selected() const noexcept override
    {
        return m_selected;
    }
    void selected(bool value) noexcept override
    {
        m_selected = value;
    }

    // ------------------------------------------------------------
    // Light properties
    // ------------------------------------------------------------

    [[nodiscard]] Type type() const noexcept
    {
        return m_type;
    }
    void type(Type t) noexcept
    {
        m_type = t;
    }

    [[nodiscard]] glm::vec3 color() const noexcept
    {
        return m_color;
    }
    void color(const glm::vec3& c) noexcept
    {
        m_color = c;
    }

    [[nodiscard]] float intensity() const noexcept
    {
        return m_intensity;
    }
    void intensity(float v) noexcept
    {
        m_intensity = v;
    }

    // Directional “sun” uses direction (unit vector).
    // Point/spot use position from model() translation.
    //
    // We can choose either:
    //  A) author direction explicitly (m_direction), or
    //  B) derive direction from model() orientation.
    //
    // For now keep it explicit to match your current workflow.

    [[nodiscard]] glm::vec3 direction() const noexcept
    {
        return m_direction;
    }
    void direction(const glm::vec3& d) noexcept
    {
        m_direction = d;
    }

    [[nodiscard]] float range() const noexcept
    {
        return m_range;
    }
    void range(float r) noexcept
    {
        m_range = r;
    }

    [[nodiscard]] float spotInnerAngleRad() const noexcept
    {
        return m_spotInnerAngleRad;
    }
    [[nodiscard]] float spotOuterAngleRad() const noexcept
    {
        return m_spotOuterAngleRad;
    }
    void spotAnglesRad(float innerRad, float outerRad) noexcept
    {
        m_spotInnerAngleRad = innerRad;
        m_spotOuterAngleRad = outerRad;
    }

    // Transform setter for later (gizmo etc.)
    void model(const glm::mat4& m) noexcept
    {
        m_model = m;
    }

    // Helpers
    [[nodiscard]] glm::vec3 position() const noexcept
    {
        return glm::vec3(m_model[3]); // translation
    }

private:
    Type      m_type  = Type::DIRECTIONAL;
    glm::mat4 m_model = glm::mat4(1.0f);

    bool m_visible  = true;
    bool m_selected = false;

    glm::vec3 m_color     = glm::vec3(1.0f);
    float     m_intensity = 2.0f;

    // Directional / Spot
    glm::vec3 m_direction = glm::normalize(glm::vec3(0.3f, 0.7f, 0.2f));

    // Point / Spot attenuation range (editor friendly)
    float m_range = 25.0f;

    // Spot
    float m_spotInnerAngleRad = 0.35f;
    float m_spotOuterAngleRad = 0.55f;
};
