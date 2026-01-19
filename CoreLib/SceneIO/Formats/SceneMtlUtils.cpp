#include "SceneMtlUtils.hpp"

#include <algorithm>
#include <cmath>
#include <glm/vec4.hpp>

#include "Material.hpp"
#include "Scene.hpp"

// -----------------------------------------------------------------------------
// Local helpers (not exposed in header)
// -----------------------------------------------------------------------------
namespace
{

    std::filesystem::path normFromMtlDir(const std::filesystem::path& mtlFile,
                                         const std::string&           rel)
    {
        if (rel.empty())
            return {};

        std::filesystem::path p = mtlFile.parent_path() / rel;

        // Normalize "..", ".", slashes, etc. If this throws on your platform, you
        // can replace with lexically_normal().
        std::error_code       ec;
        std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
        return ec ? p.lexically_normal() : canon;
    }

    inline float saturate(float x)
    {
        return std::max(0.f, std::min(1.f, x));
    }

    inline float luminance(const glm::vec3& c)
    {
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    }

    // Roughness <-> Ns (Blinn-Phong exponent) conversions
    inline float roughness_from_Ns(float Ns)
    {
        // Clamp Ns into a sane BP range and give a reasonable default if missing
        if (!(Ns > 0.0f))
            Ns = 200.0f; // many MTLs omit Ns; this gives r≈0.1–0.2
        Ns = std::max(1.0f, std::min(1000.0f, Ns));
        // Standard mapping: r = sqrt(2 / (Ns + 2))
        float r = std::sqrt(2.f / (Ns + 2.f));
        // Keep a small floor to avoid “dead matte” look
        return std::max(0.04f, std::min(1.0f, r));
    }

    inline float Ns_from_roughness(float r)
    {
        r = std::max(0.04f, std::min(1.0f, r));
        return std::max(1.0f, std::min(1000.0f, (2.f / (r * r)) - 2.f));
    }

    inline float srgbToLin(float c)
    {
        return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    inline float linToSrgb(float c)
    {
        return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    }

    inline glm::vec3 srgbToLin(const glm::vec3& c)
    {
        return {srgbToLin(c.r), srgbToLin(c.g), srgbToLin(c.b)};
    }

    inline glm::vec3 linToSrgb(const glm::vec3& c)
    {
        return {linToSrgb(c.r), linToSrgb(c.g), linToSrgb(c.b)};
    }

    // NOTE: In your old system these packed texture handles (-1 <-> 0) were used.
    // For now we only need them conceptually; you can hook them to your image manager later.
    inline uint32_t packHandle(int h)
    {
        return (h < 0) ? 0u : (static_cast<uint32_t>(h) + 1u);
    }
    inline int unpackHandle(uint32_t u)
    {
        return (u == 0u) ? -1 : static_cast<int>(u - 1u);
    }

    static inline std::string pathForImageId(const Scene* scene, ImageId id)
    {
        if (!scene || id == kInvalidImageId)
            return {};

        const ImageHandler* imgHandler = scene->imageHandler();
        if (!imgHandler)
            return {};

        // Adapt this to actual ImageHandler API
        // e.g. imgHandler->imageInfo(id)->filePath, imgHandler->filePath(id), etc.
        // const ImageInfo*
        const Image* image = imgHandler->get(id); // or whatever you use

        if (!image)
            return {};

        // Old behavior: just return the stored path string as-is.
        // That path should already be whatever loaded (relative or absolute).
        return image->path().string();
    }
} // namespace

// -----------------------------------------------------------------------------
// toMTL: Material (PBR) -> MtlFields (MTL space)
// -----------------------------------------------------------------------------
MtlFields toMTL(const Material& pbr, const Scene* scene)
{
    MtlFields mtl;

    // Engine stores linear; convert to sRGB for MTL
    const glm::vec4 baseLin  = glm::vec4(pbr.baseColor(), 1.f); // linear RGBA
    const glm::vec3 baseSrgb = linToSrgb(glm::vec3(baseLin));

    const float metallic  = saturate(pbr.metallic());
    const float roughness = saturate(pbr.roughness());

    // Split base into Kd/Ks like a metallic workflow, then write in sRGB
    const glm::vec3 F0_lin(0.04f);               // linear
    const glm::vec3 F0_srgb = linToSrgb(F0_lin); // write in sRGB

    // Ks in MTL is the specular color (approx metal/dielectric mix)
    const glm::vec3 Ks_srgb = (1.0f - metallic) * F0_srgb + metallic * baseSrgb;
    const glm::vec3 Kd_srgb = baseSrgb * (1.0f - metallic);

    mtl.Ks = Ks_srgb;
    mtl.Kd = Kd_srgb;
    mtl.Ke = linToSrgb(pbr.emissiveColor()); // emissive also as sRGB in MTL
    mtl.Ns = Ns_from_roughness(roughness);
    mtl.Ni = pbr.ior();

    // Opacity: prefer 'd' when saving (most tools expect that)
    mtl.d  = saturate(pbr.opacity());
    mtl.Tr = 1.0f - mtl.d;

    mtl.Ka = glm::vec3(0.0f);
    mtl.Tf = glm::vec3(0.0f);

    if (scene)
    {
        if (pbr.baseColorTexture() != kInvalidImageId)
            mtl.map_Kd = pathForImageId(scene, pbr.baseColorTexture());

        if (pbr.normalTexture() != kInvalidImageId)
            mtl.map_bump = pathForImageId(scene, pbr.normalTexture());

        if (pbr.emissiveTexture() != kInvalidImageId)
            mtl.map_Ke = pathForImageId(scene, pbr.emissiveTexture());
    }

    return mtl;
}

ImageId ensureImageForMap(Scene*                       scene,
                          const std::filesystem::path& absPath,
                          bool /*srgb*/)
{
    if (!scene || absPath.empty())
        return kInvalidImageId;

    ImageHandler* imgHandler = scene->imageHandler();
    if (!imgHandler)
        return kInvalidImageId;

    // MTL textures are standard 2D images; vertical flip usually desired
    // to match typical UV conventions (same as other imports).
    //
    // ImageHandler already normalizes the path via PathUtil internally.
    const ImageId id = imgHandler->loadFromFile(absPath, /*flipY*/ true);
    return id;
}

// -----------------------------------------------------------------------------
// fromMTL: MtlFields (MTL space) -> Material (PBR)
// -----------------------------------------------------------------------------
void fromMTL(Scene* scene, Material& dst, const MtlFields& m, const std::filesystem::path& mtlFile)
{
    // Colors read from MTL are in sRGB → convert to linear
    const glm::vec3 Kd_lin = srgbToLin(m.Kd);
    const glm::vec3 Ks_lin = srgbToLin(m.Ks);
    const glm::vec3 Ke_lin = srgbToLin(m.Ke);

    // Roughness from Ns (with safe default)
    float roughness = roughness_from_Ns(m.Ns);

    // Metallic heuristic from Ks magnitude vs dielectric F0 (~0.04)
    const float ksLum         = luminance(Ks_lin);
    const float F0_dielectric = 0.04f;
    float       metallic      = (ksLum - F0_dielectric) / (1.0f - F0_dielectric);
    metallic                  = std::isfinite(metallic) ? saturate(metallic) : 0.0f;

    // Base color from Kd/Ks (linear)
    glm::vec3 baseLin = (1.0f - metallic) * Kd_lin + metallic * Ks_lin;

    // Opacity: most exporters write 'd' (opaque=1); some write 'Tr' (transparent=1)
    float opacity = 1.0f;
    if (m.d >= 0.0f && m.d <= 1.0f)
        opacity = saturate(m.d);
    else if (m.Tr >= 0.0f && m.Tr <= 1.0f)
        opacity = saturate(1.0f - m.Tr);

    const float ior = (m.Ni > 0.f) ? m.Ni : 1.5f;

    // ------------------------------------------------------------------------
    // Legacy-friendly tweaks (same idea as your old code)
    // ------------------------------------------------------------------------

    // 1) Older MTLs with no specular and no Ns: give a friendlier gloss default
    if (!(m.Ns > 0.0f) && glm::length(Ks_lin) < 1e-4f)
        roughness = 0.4f; // moderate highlight instead of fully matte

    // 2) If a base color texture is present, avoid double-darkening:
    if (!m.map_Kd.empty())
        baseLin = glm::max(baseLin, glm::vec3(0.8f));

    // 3) Clamp extremes
    metallic  = saturate(metallic);
    roughness = std::clamp(roughness, 0.04f, 1.0f);

    // ------------------------------------------------------------------------
    // Write PBR fields (Material expects linear)
    // ------------------------------------------------------------------------
    dst.baseColor(baseLin);
    dst.metallic(metallic);
    dst.roughness(roughness);
    dst.opacity(opacity);
    dst.ior(ior);

    // Emissive: store color in emissiveColor and drive intensity separately.
    dst.emissiveColor(Ke_lin);
    dst.emissiveIntensity(1.0f); // you can later pull length(Ke_lin) into intensity if you want

    // Alpha mode: simple heuristic – if opacity < 1, use Blend.
    if (opacity < 1.0f)
        dst.alphaMode(Material::AlphaMode::Blend);
    else
        dst.alphaMode(Material::AlphaMode::Opaque);

    // ------------------------------------------------------------------------
    // Textures → ImageId (Material stores ImageId, NOT TextureId)
    // ------------------------------------------------------------------------

    // Base color texture (map_Kd)
    if (!m.map_Kd.empty())
    {
        const auto    p     = normFromMtlDir(mtlFile, m.map_Kd);
        const ImageId imgId = ensureImageForMap(scene, p, /*srgb*/ true);
        if (imgId != kInvalidImageId)
            dst.baseColorTexture(imgId);
    }

    // Normal / bump texture (map_bump)
    if (!m.map_bump.empty())
    {
        const auto    p     = normFromMtlDir(mtlFile, m.map_bump);
        const ImageId imgId = ensureImageForMap(scene, p, /*srgb*/ false);
        if (imgId != kInvalidImageId)
            dst.normalTexture(imgId);
    }

    // Emissive texture (map_Ke)
    if (!m.map_Ke.empty())
    {
        const auto    p     = normFromMtlDir(mtlFile, m.map_Ke);
        const ImageId imgId = ensureImageForMap(scene, p, /*srgb*/ true);
        if (imgId != kInvalidImageId)
            dst.emissiveTexture(imgId);
    }

    // Later also use:
    // - map_Ks (specular) → maybe mraoTexture or custom slot
    // - map_Tr (opacity)  → alpha/opacity map if add support
}

// -----------------------------------------------------------------------------
// sanitizeName
// -----------------------------------------------------------------------------
std::string sanitizeName(const std::string& input)
{
    std::string output;
    output.reserve(input.size());
    for (char c : input)
    {
        if (c == ' ')
        {
            output += '_';
        }
        else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')
        {
            output += c;
        }
        // skip everything else
    }

    if (output.empty())
        return "unnamed";
    return output;
}
