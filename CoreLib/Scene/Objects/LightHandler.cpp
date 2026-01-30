//============================================================
// LightHandler.cpp
//============================================================
#include "LightHandler.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <utility>

#include "CoreUtilities.hpp"

LightHandler::LightHandler() : m_changeCounter{std::make_shared<SysCounter>()}
{
}

void LightHandler::clear() noexcept
{
    m_lights.clear();
    m_changeCounter->change();
}

LightId LightHandler::createLight(std::string_view name, LightType type) noexcept
{
    Light l = {};
    l.name  = std::string(name);
    l.type  = type;

    sanitize(l);

    const uint32_t idx = m_lights.insert(l);
    const LightId  id  = static_cast<LightId>(idx);

    // Stores the assigned stable ID inside the light instance as well.
    // This is useful for debugging and for UI code that wants to display IDs.
    m_lights[id].id = id;

    m_changeCounter->change();
    return id;
}

LightId LightHandler::createLight(const Light& src) noexcept
{
    Light l = src;
    sanitize(l);

    const uint32_t idx = m_lights.insert(l);
    const LightId  id  = static_cast<LightId>(idx);

    // Stores the assigned stable ID inside the light instance as well.
    // This is useful for debugging and for UI code that wants to display IDs.
    m_lights[id].id = id;

    m_changeCounter->change();
    return id;
}

bool LightHandler::destroyLight(LightId id) noexcept
{
    if (id == kInvalidLightId)
        return false;

    // HoleList does not currently expose a direct validity query for an index.
    // The occupied indices list is used as the authoritative validity check.
    const auto& ids = m_lights.valid_indices();
    const bool  ok  = (std::find(ids.begin(), ids.end(), id) != ids.end());
    if (!ok)
        return false;

    m_lights.remove(id);
    m_changeCounter->change();
    return true;
}

Light* LightHandler::light(LightId id) noexcept
{
    if (id == kInvalidLightId)
        return nullptr;

    const auto& ids = m_lights.valid_indices();
    if (std::find(ids.begin(), ids.end(), id) == ids.end())
        return nullptr;

    return &m_lights[id];
}

const Light* LightHandler::light(LightId id) const noexcept
{
    if (id == kInvalidLightId)
        return nullptr;

    const auto& ids = m_lights.valid_indices();
    if (std::find(ids.begin(), ids.end(), id) == ids.end())
        return nullptr;

    return &m_lights[id];
}

std::vector<LightId> LightHandler::allLights() const
{
    // valid_indices() returns a cached vector of occupied indices.
    return m_lights.valid_indices();
}

bool LightHandler::setEnabled(LightId id, bool enabled) noexcept
{
    Light* l = light(id);
    if (!l)
        return false;

    if (l->enabled == enabled)
        return true;

    l->enabled = enabled;
    m_changeCounter->change();
    return true;
}

// ------------------------------------------------------------
// sanitize()
// ------------------------------------------------------------

void LightHandler::sanitize(Light& l) noexcept
{
    // ID is assigned by the handler.
    l.id = kInvalidLightId;

    // Scalars
    if (!std::isfinite(l.intensity) || l.intensity < 0.0f)
        l.intensity = 0.0f;

    if (!std::isfinite(l.range) || l.range < 0.0f)
        l.range = 0.0f;

    // Direction: prefers the core helper; falls back to -Z if degenerate.
    glm::vec3 d = un::safe_normalize(l.direction);
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) || glm::dot(d, d) <= 0.0f)
        d = glm::vec3(0.0f, 0.0f, -1.0f);
    l.direction = d;

    // Spot cones: finite + ordered.
    if (!std::isfinite(l.spotInnerConeRad) || l.spotInnerConeRad < 0.0f)
        l.spotInnerConeRad = 0.0f;

    if (!std::isfinite(l.spotOuterConeRad) || l.spotOuterConeRad < 0.0f)
        l.spotOuterConeRad = 0.0f;

    if (l.spotOuterConeRad < l.spotInnerConeRad)
        std::swap(l.spotOuterConeRad, l.spotInnerConeRad);

    // Ensures spot lights have a usable cone even if the source data is empty.
    if (l.type == LightType::Spot && l.spotOuterConeRad <= 0.0f)
        l.spotOuterConeRad = 0.78539816339f; // ~pi/4

    // Color: finite + non-negative (HDR allowed, so no clamp to 1).
    if (!std::isfinite(l.color.x) || l.color.x < 0.0f)
        l.color.x = 0.0f;
    if (!std::isfinite(l.color.y) || l.color.y < 0.0f)
        l.color.y = 0.0f;
    if (!std::isfinite(l.color.z) || l.color.z < 0.0f)
        l.color.z = 0.0f;

    // Position: finite.
    if (!std::isfinite(l.position.x))
        l.position.x = 0.0f;
    if (!std::isfinite(l.position.y))
        l.position.y = 0.0f;
    if (!std::isfinite(l.position.z))
        l.position.z = 0.0f;
}
