#include "GpuMaterial.hpp"

#include "Material.hpp"
#include "TextureHandler.hpp" // NEW – where TextureHandler, TextureId, TextureDesc, TextureUsage live

GpuMaterial toGpuMaterial(const Material& m)
{
    GpuMaterial out{};

    out.baseColor         = m.baseColor();
    out.opacity           = m.opacity();
    out.emissiveColor     = m.emissiveColor();
    out.emissiveIntensity = m.emissiveIntensity();
    out.roughness         = m.roughness();
    out.metallic          = m.metallic();
    out.ior               = m.ior();
    out.pad0              = 0.0f;

    // Texture indices are filled in buildGpuMaterialArray()
    out.baseColorTexture = -1;
    out.normalTexture    = -1;
    out.mraoTexture      = -1;
    out.emissiveTexture  = -1;

    return out;
}

void buildGpuMaterialArray(const std::vector<Material>& src,
                           TextureHandler&              texHandler,
                           std::vector<GpuMaterial>&    dst)
{
    dst.clear();
    dst.reserve(src.size());

    // Common texture desc presets based on your enum class TextureUsage { Color, Normal, Data };
    TextureDesc baseDesc{};
    baseDesc.usage           = TextureUsage::Color;
    baseDesc.generateMipmaps = true;
    baseDesc.srgb            = true; // color maps in sRGB

    TextureDesc normalDesc{};
    normalDesc.usage           = TextureUsage::Normal;
    normalDesc.generateMipmaps = true;
    normalDesc.srgb            = false; // normals are linear data

    TextureDesc mraoDesc{};
    mraoDesc.usage           = TextureUsage::Data;
    mraoDesc.generateMipmaps = true;
    mraoDesc.srgb            = false; // MRAO is linear

    TextureDesc emissiveDesc{};
    emissiveDesc.usage           = TextureUsage::Color;
    emissiveDesc.generateMipmaps = true;
    emissiveDesc.srgb            = true; // emissive is color

    for (const Material& m : src)
    {
        GpuMaterial gm = toGpuMaterial(m);

        // baseColorTexture (Material stores ImageId)
        if (m.baseColorTexture() != kInvalidImageId)
        {
            TextureId tid = texHandler.ensureTexture(
                m.baseColorTexture(),
                baseDesc,
                m.name() + "_BaseColor");
            gm.baseColorTexture = tid; // -1 or >= 0
        }
        else
        {
            gm.baseColorTexture = -1;
        }

        // normalTexture
        if (m.normalTexture() != kInvalidImageId)
        {
            TextureId tid = texHandler.ensureTexture(
                m.normalTexture(),
                normalDesc,
                m.name() + "_Normal");
            gm.normalTexture = tid;
        }
        else
        {
            gm.normalTexture = -1;
        }

        // mraoTexture (Data)
        if (m.mraoTexture() != kInvalidImageId)
        {
            TextureId tid = texHandler.ensureTexture(
                m.mraoTexture(),
                mraoDesc,
                m.name() + "_MRAO");
            gm.mraoTexture = tid;
        }
        else
        {
            gm.mraoTexture = -1;
        }

        // emissiveTexture
        if (m.emissiveTexture() != kInvalidImageId)
        {
            TextureId tid = texHandler.ensureTexture(
                m.emissiveTexture(),
                emissiveDesc,
                m.name() + "_Emissive");
            gm.emissiveTexture = tid;
        }
        else
        {
            gm.emissiveTexture = -1;
        }

        dst.push_back(gm);
    }
}
