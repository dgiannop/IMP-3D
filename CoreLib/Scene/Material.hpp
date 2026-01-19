#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

#include "ImageHandler.hpp" // for ImageId
#include "SysCounter.hpp"

/// \brief PBR-style material used by Scene and GPU.
///
/// This is the high-level CPU-side representation. It is:
/// - friendly for OBJ / glTF IO,
/// - easy to convert to a compact GPU struct (MaterialGpu),
/// - referenced by index (materialId) from meshes.
class Material
{
public:
    /// Alpha blending mode for transparency.
    enum class AlphaMode : std::uint8_t
    {
        Opaque, ///< No transparency, ignore opacity.
        Mask,   ///< Cutout (alpha test).
        Blend   ///< Standard alpha blending.
    };

    Material() = default;
    explicit Material(std::string name);

    // -----------------------------
    // Identity
    // -----------------------------

    const std::string& name() const noexcept;

    void name(const std::string& name);

    /// Optional stable numeric ID (typically its index in SceneMaterials).
    std::uint32_t id() const noexcept;
    void          id(std::uint32_t id) noexcept;

    // -----------------------------
    // Core PBR parameters
    // -----------------------------
    /// Base color (albedo) in linear space.
    const glm::vec3& baseColor() const noexcept;
    void             baseColor(const glm::vec3& color) noexcept;

    /// Opacity in [0, 1]. 1 = fully opaque.
    float opacity() const noexcept;
    void  opacity(float value) noexcept;

    /// Emissive color in linear space.
    const glm::vec3& emissiveColor() const noexcept;
    void             emissiveColor(const glm::vec3& color) noexcept;

    /// Emissive intensity multiplier.
    float emissiveIntensity() const noexcept;
    void  emissiveIntensity(float value) noexcept;

    /// Roughness in [0, 1].
    float roughness() const noexcept;
    void  roughness(float value) noexcept;

    /// Metallic in [0, 1].
    float metallic() const noexcept;
    void  metallic(float value) noexcept;

    /// Index of refraction for dielectrics (e.g. 1.5 for glass/plastic).
    float ior() const noexcept;
    void  ior(float value) noexcept;

    bool doubleSided() const noexcept;
    void doubleSided(bool value) noexcept;

    // -----------------------------
    // Texture slots
    // Indices reference SceneTextures or similar.
    // -1 means "no texture".
    // -----------------------------
    ImageId baseColorTexture() const noexcept;
    void    baseColorTexture(ImageId id) noexcept;

    ImageId normalTexture() const noexcept;
    void    normalTexture(ImageId id) noexcept;

    ImageId mraoTexture() const noexcept;
    void    mraoTexture(ImageId id) noexcept;

    ImageId emissiveTexture() const noexcept;
    void    emissiveTexture(ImageId id) noexcept;

    // -----------------------------
    // Rendering modes / flags
    // -----------------------------
    AlphaMode alphaMode() const noexcept;
    void      alphaMode(AlphaMode mode) noexcept;

    static Material makeDefault()
    {
        return Material("Default");
    }

    SysCounterPtr changeCounter() const noexcept;

private:
    // Identity
    std::string   m_name;
    std::uint32_t m_id = 0; ///< Scene-assigned id (index in material array).

    // Core PBR parameters
    glm::vec3 m_baseColor = {1.f, 1.f, 1.f};
    float     m_opacity   = 1.f;

    glm::vec3 m_emissiveColor     = {0.f, 0.f, 0.f};
    float     m_emissiveIntensity = 0.f;

    float m_roughness = 0.5f;
    float m_metallic  = 0.0f;
    float m_ior       = 1.5f;

    AlphaMode m_alphaMode   = AlphaMode::Opaque;
    bool      m_doubleSided = false;

    ImageId m_baseColorTex = kInvalidImageId;
    ImageId m_normalTex    = kInvalidImageId;
    ImageId m_mraoTex      = kInvalidImageId;
    ImageId m_emissiveTex  = kInvalidImageId;

    SysCounterPtr m_changeCounter;
};
