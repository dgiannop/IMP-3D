#include "Material.hpp"

#include <algorithm> // std::clamp

Material::Material(std::string name) : m_name{std::move(name)}, m_changeCounter{std::make_shared<SysCounter>()}
{
}

// -----------------------------
// Identity
// -----------------------------
const std::string& Material::name() const noexcept
{
    return m_name;
}

void Material::name(const std::string& name)
{
    m_name = name;
    m_changeCounter->change();
}

std::uint32_t Material::id() const noexcept
{
    return m_id;
}

void Material::id(std::uint32_t id) noexcept
{
    m_id = id;
    m_changeCounter->change();
}

// -----------------------------
// Core PBR parameters
// -----------------------------
const glm::vec3& Material::baseColor() const noexcept
{
    return m_baseColor;
}

void Material::baseColor(const glm::vec3& color) noexcept
{
    m_baseColor = color;
    m_changeCounter->change();
}

float Material::opacity() const noexcept
{
    return m_opacity;
}

void Material::opacity(float value) noexcept
{
    m_opacity = std::clamp(value, 0.0f, 1.0f);
    m_changeCounter->change();
}

const glm::vec3& Material::emissiveColor() const noexcept
{
    return m_emissiveColor;
}

void Material::emissiveColor(const glm::vec3& color) noexcept
{
    m_emissiveColor = color;
    m_changeCounter->change();
}

float Material::emissiveIntensity() const noexcept
{
    return m_emissiveIntensity;
}

void Material::emissiveIntensity(float value) noexcept
{
    m_emissiveIntensity = std::max(0.0f, value);
    m_changeCounter->change();
}

float Material::roughness() const noexcept
{
    return m_roughness;
}

void Material::roughness(float value) noexcept
{
    m_roughness = std::clamp(value, 0.0f, 1.0f);
    m_changeCounter->change();
}

float Material::metallic() const noexcept
{
    return m_metallic;
}

void Material::metallic(float value) noexcept
{
    m_metallic = std::clamp(value, 0.0f, 1.0f);
    m_changeCounter->change();
}

float Material::ior() const noexcept
{
    return m_ior;
}

void Material::ior(float value) noexcept
{
    // Reasonable clamping range for dielectrics/metals
    m_ior = std::clamp(value, 1.0f, 3.0f);
    m_changeCounter->change();
}

// -----------------------------
// Texture slots
// -----------------------------
ImageId Material::baseColorTexture() const noexcept
{
    return m_baseColorTex;
}

void Material::baseColorTexture(ImageId id) noexcept
{
    m_baseColorTex = id;
    m_changeCounter->change();
}

ImageId Material::normalTexture() const noexcept
{
    return m_normalTex;
}

void Material::normalTexture(ImageId id) noexcept
{
    m_normalTex = id;
    m_changeCounter->change();
}

ImageId Material::mraoTexture() const noexcept
{
    return m_mraoTex;
}

/*
 * float ao        = texture(mraoTex, uv).r;
 * float roughness = texture(mraoTex, uv).g;
 * float metallic  = texture(mraoTex, uv).b;
 */
void Material::mraoTexture(ImageId id) noexcept
{
    m_mraoTex = id;
    m_changeCounter->change();
}

ImageId Material::emissiveTexture() const noexcept
{
    return m_emissiveTex;
}

void Material::emissiveTexture(ImageId id) noexcept
{
    m_emissiveTex = id;
    m_changeCounter->change();
}

// -----------------------------
// Rendering modes / flags
// -----------------------------
Material::AlphaMode Material::alphaMode() const noexcept
{
    return m_alphaMode;
}

void Material::alphaMode(AlphaMode mode) noexcept
{
    m_alphaMode = mode;
    m_changeCounter->change();
}

bool Material::doubleSided() const noexcept
{
    return m_doubleSided;
}

void Material::doubleSided(bool value) noexcept
{
    m_doubleSided = value;
    m_changeCounter->change();
}

// -----------------------------
// Counter
// -----------------------------
SysCounterPtr Material::changeCounter() const noexcept
{
    return m_changeCounter;
}
