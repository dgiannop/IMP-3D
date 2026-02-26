//============================================================
// SceneLight.cpp  (FULL REPLACEMENT)
//============================================================
#include "SceneLight.hpp"

#include <cmath>
#include <glm/geometric.hpp>

#include "CoreUtilities.hpp" // un::safe_normalize
#include "LightHandler.hpp"

namespace
{
    static bool finite3(const glm::vec3& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    static float clamp_nonneg_finite(float v, float fallback = 0.0f) noexcept
    {
        if (!std::isfinite(v) || v < 0.0f)
            return fallback;
        return v;
    }

    static glm::vec3 sanitize_color(const glm::vec3& c) noexcept
    {
        glm::vec3 r = c;
        if (!finite3(r))
            r = glm::vec3(0.0f);

        // HDR allowed, but no negatives.
        r.x = (std::isfinite(r.x) && r.x >= 0.0f) ? r.x : 0.0f;
        r.y = (std::isfinite(r.y) && r.y >= 0.0f) ? r.y : 0.0f;
        r.z = (std::isfinite(r.z) && r.z >= 0.0f) ? r.z : 0.0f;
        return r;
    }

    static glm::vec3 sanitize_direction(const glm::vec3& d) noexcept
    {
        glm::vec3 n = un::safe_normalize(d);
        if (!finite3(n) || glm::dot(n, n) <= 0.0f)
            n = glm::vec3(0.0f, 0.0f, -1.0f);
        return n;
    }

    static bool is_blank(std::string_view s) noexcept
    {
        for (const char ch : s)
        {
            if (!std::isspace(static_cast<unsigned char>(ch)))
                return false;
        }
        return true;
    }

} // namespace

SceneLight::SceneLight(LightHandler* lightHandler, LightId id, std::string_view name) :
    m_lightHandler(lightHandler),
    m_lightId(id),
    m_model(glm::mat4{1.f}),
    m_visible(true),
    m_selected(false),
    m_name(name)
{
    // Initialize m_model translation from the underlying light position if available.
    if (const Light* l = lightPtr())
    {
        m_model[3][0] = l->position.x;
        m_model[3][1] = l->position.y;
        m_model[3][2] = l->position.z;

        // If a non-empty display name was provided, sync it to the underlying Light.
        // Otherwise, adopt the Light's name.
        if (!m_name.empty() && !is_blank(m_name))
        {
            if (l->name != m_name)
            {
                Light* lw = lightPtr();
                if (lw)
                {
                    lw->name = m_name;
                    notifyChanged();
                }
            }
        }
        else
        {
            m_name = l->name;
        }
    }
}

void SceneLight::idle(Scene* /*scene*/)
{
    // No behavior yet. Reserved for future editor interactions/gizmos.
}

glm::mat4 SceneLight::model() const noexcept
{
    return m_model;
}

void SceneLight::model(const glm::mat4& mtx) noexcept
{
    m_model = mtx;

    // Keep underlying Light WORLD position in sync with transform translation.
    Light* l = lightPtr();
    if (!l)
        return;

    const glm::vec3 p(mtx[3][0], mtx[3][1], mtx[3][2]);
    if (l->position == p)
        return;

    l->position = p;
    notifyChanged();
}

bool SceneLight::visible() const noexcept
{
    return m_visible;
}

void SceneLight::visible(bool value) noexcept
{
    m_visible = value;
    // (No handler change; visibility is SceneObject-side for now.)
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

void SceneLight::name(std::string_view n) noexcept
{
    if (n.empty() || is_blank(n))
        return;

    // Update scene object label
    if (m_name != n)
        m_name.assign(n.data(), n.size());

    // Update authoritative light name
    Light* l = lightPtr();
    if (l && l->name != m_name)
    {
        l->name = m_name;
        notifyChanged();
    }
}

// ------------------------------------------------------------
// Light access
// ------------------------------------------------------------

Light* SceneLight::lightPtr() noexcept
{
    if (!m_lightHandler || m_lightId == kInvalidLightId)
        return nullptr;

    return m_lightHandler->light(m_lightId);
}

const Light* SceneLight::lightPtr() const noexcept
{
    if (!m_lightHandler || m_lightId == kInvalidLightId)
        return nullptr;

    return m_lightHandler->light(m_lightId);
}

void SceneLight::notifyChanged() noexcept
{
    if (!m_lightHandler)
        return;

    const SysCounterPtr c = m_lightHandler->changeCounter();
    if (c)
        c->change();
}

// ------------------------------------------------------------
// Delegated Light parameters
// ------------------------------------------------------------

bool SceneLight::enabled() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->enabled : false;
}

void SceneLight::enabled(bool v) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    if (l->enabled == v)
        return;

    l->enabled = v;
    notifyChanged();
}

LightType SceneLight::lightType() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->type : LightType::Directional;
}

void SceneLight::lightType(LightType t) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    if (l->type == t)
        return;

    l->type = t;

    // Minimal sanitize for spot cones ordering / defaults.
    if (!std::isfinite(l->spotInnerConeRad) || l->spotInnerConeRad < 0.0f)
        l->spotInnerConeRad = 0.0f;

    if (!std::isfinite(l->spotOuterConeRad) || l->spotOuterConeRad < 0.0f)
        l->spotOuterConeRad = 0.0f;

    if (l->spotOuterConeRad < l->spotInnerConeRad)
        std::swap(l->spotOuterConeRad, l->spotInnerConeRad);

    if (l->type == LightType::Spot && l->spotOuterConeRad <= 0.0f)
        l->spotOuterConeRad = 0.78539816339f; // ~pi/4

    notifyChanged();
}

glm::vec3 SceneLight::color() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->color : glm::vec3{1.0f};
}

void SceneLight::color(const glm::vec3& c) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    const glm::vec3 nc = sanitize_color(c);
    if (l->color == nc)
        return;

    l->color = nc;
    notifyChanged();
}

float SceneLight::intensity() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->intensity : 0.0f;
}

void SceneLight::intensity(float v) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    const float nv = clamp_nonneg_finite(v, 0.0f);
    if (l->intensity == nv)
        return;

    l->intensity = nv;
    notifyChanged();
}

float SceneLight::range() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->range : 0.0f;
}

void SceneLight::range(float v) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    const float nv = clamp_nonneg_finite(v, 0.0f);
    if (l->range == nv)
        return;

    l->range = nv;
    notifyChanged();
}

float SceneLight::spotInnerConeRad() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->spotInnerConeRad : 0.0f;
}

void SceneLight::spotInnerConeRad(float r) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    float inner = clamp_nonneg_finite(r, 0.0f);
    float outer = clamp_nonneg_finite(l->spotOuterConeRad, 0.0f);

    if (outer < inner)
        inner = outer;

    if (l->spotInnerConeRad == inner)
        return;

    l->spotInnerConeRad = inner;
    notifyChanged();
}

float SceneLight::spotOuterConeRad() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->spotOuterConeRad : 0.0f;
}

void SceneLight::spotOuterConeRad(float r) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    float outer = clamp_nonneg_finite(r, 0.0f);
    float inner = clamp_nonneg_finite(l->spotInnerConeRad, 0.0f);

    if (outer < inner)
        inner = outer;

    const bool changed = (l->spotOuterConeRad != outer) || (l->spotInnerConeRad != inner);
    if (!changed)
        return;

    l->spotOuterConeRad = outer;
    l->spotInnerConeRad = inner;

    if (l->type == LightType::Spot && l->spotOuterConeRad <= 0.0f)
        l->spotOuterConeRad = 0.78539816339f; // ~pi/4

    notifyChanged();
}

// ------------------------------------------------------------
// Convenience: position/direction (WORLD space)
// ------------------------------------------------------------

glm::vec3 SceneLight::position() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->position : glm::vec3(0.0f);
}

void SceneLight::position(const glm::vec3& p) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    glm::vec3 np = p;
    if (!finite3(np))
        np = glm::vec3(0.0f);

    if (l->position == np)
        return;

    l->position = np;

    // Keep m_model translation in sync.
    m_model[3][0] = np.x;
    m_model[3][1] = np.y;
    m_model[3][2] = np.z;

    notifyChanged();
}

glm::vec3 SceneLight::direction() const noexcept
{
    const Light* l = lightPtr();
    return l ? l->direction : glm::vec3(0.0f, 0.0f, -1.0f);
}

void SceneLight::direction(const glm::vec3& d) noexcept
{
    Light* l = lightPtr();
    if (!l)
        return;

    const glm::vec3 nd = sanitize_direction(d);
    if (l->direction == nd)
        return;

    l->direction = nd;
    notifyChanged();
}
