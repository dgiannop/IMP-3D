#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>

#include "GpuResources/TextureHandler.hpp"

class Material;

/**
 * @brief GPU-side PBR material layout for SSBO/UBO upload.
 *
 * This is backend-agnostic (no Vulkan/OpenGL types) and must match
 * the std430/std140 layout used in your shaders.
 *
 * Notes:
 *  - Texture indices are -1 when the texture slot is unused.
 *  - doubleSided and alphaMode are *not* included for now, since
 *    they mainly influence pipeline/cull/blend state on the CPU.
 */
struct GpuMaterial
{
    glm::vec3 baseColor; ///< linear-space albedo
    float     opacity;   ///< [0,1], 1 = opaque

    glm::vec3 emissiveColor;     ///< linear-space emissive color
    float     emissiveIntensity; ///< emissive strength multiplier

    float roughness; ///< [0,1]
    float metallic;  ///< [0,1]
    float ior;       ///< index of refraction for dielectrics
    float pad0;      ///< padding / reserved (keep 16-byte alignment)

    // ----- texture indices -----
    //
    // Indices into whatever texture table your renderer uses
    // (e.g. SceneTextures). -1 means "no texture bound".

    std::int32_t baseColorTexture = -1;
    std::int32_t normalTexture    = -1;
    std::int32_t mraoTexture      = -1; ///< combined metal/rough/ao, if used
    std::int32_t emissiveTexture  = -1;
};

/**
 * @brief Convert a CPU-side Material into a GpuMaterial.
 */
GpuMaterial toGpuMaterial(const Material& src);

/**
 * @brief Build a contiguous GPU array from a list of Materials.
 */
// void buildGpuMaterialArray(const std::vector<Material>& src, std::vector<GpuMaterial>& dst);
void buildGpuMaterialArray(const std::vector<Material>& materials,
                           TextureHandler&              texHandler,
                           std::vector<GpuMaterial>&    out);
