#pragma once

#include <filesystem>
#include <glm/vec3.hpp>
#include <string>

class Scene;
class Material;

/**
 * @brief Raw MTL fields as parsed from / written to .mtl files.
 *
 * This struct lives in OBJ/MTL space (sRGB color, Ns, Ni, map_* strings),
 * not in engine space.
 */
struct MtlFields
{
    std::string name = "Default";

    glm::vec3 Ka = glm::vec3{0.f};  ///< Ambient color
    glm::vec3 Kd = glm::vec3{0.5f}; ///< Diffuse color
    glm::vec3 Ks = glm::vec3{0.f};  ///< Specular color
    glm::vec3 Ke = glm::vec3{0.f};  ///< Emission color
    glm::vec3 Tf = glm::vec3{0.f};  ///< Transmission filter (RGB)
    float     Tr = 0.f;             ///< Transparency (LW convention)
    float     Ns = 0.f;             ///< Specular exponent (Blinn-Phong)
    float     Ni = 0.f;             ///< Optical density (index of refraction)
    float     d  = 1.f;             ///< Dissolve (opacity, 1 = opaque)

    // Texture maps (raw paths as read from the MTL)
    std::string map_Ka;   ///< Ambient texture map
    std::string map_Kd;   ///< Diffuse texture map
    std::string map_Ks;   ///< Specular texture map
    std::string map_Ke;   ///< Emission texture map
    std::string map_Tr;   ///< Opacity texture map
    std::string map_bump; ///< Bump map
    std::string map_Ni;   ///< Refraction map
};

/**
 * @brief Convert an engine Material (PBR) into MtlFields (MTL representation).
 *
 * Colors in Material are assumed to be linear. MtlFields expect sRGB.
 */
MtlFields toMTL(const Material& pbr, const Scene* scene);

/**
 * @brief Convert MtlFields (from .mtl) into an engine Material (PBR).
 *
 * Colors in MtlFields are sRGB; Material expects linear.
 *
 * @param scene   Scene pointer for potential texture lookup (may be unused for now).
 * @param dst     Destination Material to fill.
 * @param m       Parsed MTL fields.
 * @param mtlFile Full path to the .mtl file (for resolving relative texture paths).
 */
void fromMTL(Scene* scene, Material& dst, const MtlFields& m, const std::filesystem::path& mtlFile);

/**
 * @brief Sanitize a material or object name for exporting.
 *
 * Replaces spaces with '_' and removes problematic characters.
 */
std::string sanitizeName(const std::string& input);
