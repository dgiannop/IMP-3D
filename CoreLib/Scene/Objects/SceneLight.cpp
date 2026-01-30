#include "SceneLight.hpp"

#include "LightHandler.hpp"

SceneLight::SceneLight(LightHandler* lightHandler, LightId id, std::string_view name) : m_lightHandler(lightHandler), m_lightId(id), m_name(name)
{
}

void SceneLight::idle(Scene* /*scene*/)
{
    // SceneLight does not require per-frame maintenance by default.
    // Animation, gizmo updates, or derived-state refresh can be introduced here later.
}

glm::mat4 SceneLight::model() const noexcept
{
    return m_model;
}

void SceneLight::model(const glm::mat4& mtx) noexcept
{
    m_model = mtx;
}

bool SceneLight::visible() const noexcept
{
    return m_visible;
}

void SceneLight::visible(bool value) noexcept
{
    m_visible = value;
}

bool SceneLight::selected() const noexcept
{
    return m_selected;
}

void SceneLight::selected(bool value) noexcept
{
    m_selected = value;
}

LightId SceneLight::lightId() const noexcept
{
    return m_lightId;
}

std::string_view SceneLight::name() const noexcept
{
    return m_name;
}
