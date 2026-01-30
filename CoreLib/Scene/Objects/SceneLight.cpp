#include "SceneLight.hpp"

#include "LightHandler.hpp"

SceneLight::SceneLight(LightHandler* lightHandler, LightId id, std::string_view name) : m_lightHandler(lightHandler), m_lightId(id), m_name(name)
{
}

void SceneLight::idle(Scene* /*scene*/)
{
}

glm::mat4 SceneLight::model() const noexcept
{
    return m_model;
}

void SceneLight::model(const glm::mat4& m) noexcept
{
    m_model = m;
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
