// ============================================================
// GltfSceneFormat.cpp
// ============================================================

#include "GltfSceneFormat.hpp"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <ImageHandler.hpp>
#include <SysMesh.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <string>
#include <vector>

#include "MaterialHandler.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"

namespace
{
    // ------------------------------------------------------------
    // Small helpers
    // ------------------------------------------------------------
    static std::string makeName(const std::string& base, int index, const char* fallbackPrefix)
    {
        if (!base.empty())
            return base;
        return std::string(fallbackPrefix) + std::to_string(index);
    }

    static glm::mat4 nodeLocalMatrix(const tinygltf::Node& n)
    {
        if (n.matrix.size() == 16)
        {
            glm::mat4 m(1.0f);
            // glTF matrices are column-major (same as glm)
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    m[c][r] = static_cast<float>(n.matrix[c * 4 + r]);
                }
            }
            return m;
        }

        glm::vec3 t(0.0f);
        glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 s(1.0f);

        if (n.translation.size() == 3)
        {
            t.x = static_cast<float>(n.translation[0]);
            t.y = static_cast<float>(n.translation[1]);
            t.z = static_cast<float>(n.translation[2]);
        }

        if (n.rotation.size() == 4)
        {
            // glTF rotation is (x,y,z,w)
            const float x = static_cast<float>(n.rotation[0]);
            const float y = static_cast<float>(n.rotation[1]);
            const float z = static_cast<float>(n.rotation[2]);
            const float w = static_cast<float>(n.rotation[3]);
            r             = glm::quat(w, x, y, z);
        }

        if (n.scale.size() == 3)
        {
            s.x = static_cast<float>(n.scale[0]);
            s.y = static_cast<float>(n.scale[1]);
            s.z = static_cast<float>(n.scale[2]);
        }

        const glm::mat4 T = glm::translate(glm::mat4(1.0f), t);
        const glm::mat4 R = glm::mat4_cast(r);
        const glm::mat4 S = glm::scale(glm::mat4(1.0f), s);
        return T * R * S;
    }

    static void buildWorldMatrices(const tinygltf::Model&  model,
                                   int                     nodeIndex,
                                   const glm::mat4&        parentWorld,
                                   std::vector<glm::mat4>& outWorld)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
            return;

        const tinygltf::Node& n     = model.nodes[nodeIndex];
        const glm::mat4       local = nodeLocalMatrix(n);
        const glm::mat4       world = parentWorld * local;

        outWorld[nodeIndex] = world;

        for (int child : n.children)
        {
            buildWorldMatrices(model, child, world, outWorld);
        }
    }

    static bool readAccessorVec3Float(const tinygltf::Model&    model,
                                      const tinygltf::Accessor& accessor,
                                      std::vector<glm::vec3>&   out,
                                      SceneIOReport&            report,
                                      const char*               label)
    {
        out.clear();

        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3)
        {
            report.error(std::string("glTF: expected VEC3/FLOAT for ") + label);
            return false;
        }

        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        {
            report.error(std::string("glTF: invalid bufferView for ") + label);
            return false;
        }

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        {
            report.error(std::string("glTF: invalid buffer index for ") + label);
            return false;
        }

        const tinygltf::Buffer& buf = model.buffers[view.buffer];

        const size_t baseOffset = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (baseOffset >= buf.data.size())
        {
            report.error(std::string("glTF: buffer range out of bounds for ") + label);
            return false;
        }

        const unsigned char* data  = buf.data.data() + baseOffset;
        const size_t         count = static_cast<size_t>(accessor.count);

        const size_t stride =
            (view.byteStride > 0)
                ? static_cast<size_t>(view.byteStride)
                : sizeof(float) * 3u;

        if (stride < sizeof(float) * 3u)
        {
            report.error(std::string("glTF: invalid stride for ") + label);
            return false;
        }

        out.resize(count);

        for (size_t i = 0; i < count; ++i)
        {
            const float* pf = reinterpret_cast<const float*>(data + i * stride);
            out[i]          = glm::vec3(pf[0], pf[1], pf[2]);
        }

        return true;
    }

    static bool readAccessorVec2Float(const tinygltf::Model&    model,
                                      const tinygltf::Accessor& accessor,
                                      std::vector<glm::vec2>&   out,
                                      SceneIOReport&            report,
                                      const char*               label)
    {
        out.clear();

        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC2)
        {
            report.error(std::string("glTF: expected VEC2/FLOAT for ") + label);
            return false;
        }

        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        {
            report.error(std::string("glTF: invalid bufferView for ") + label);
            return false;
        }

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        {
            report.error(std::string("glTF: invalid buffer index for ") + label);
            return false;
        }

        const tinygltf::Buffer& buf = model.buffers[view.buffer];

        const size_t baseOffset = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (baseOffset >= buf.data.size())
        {
            report.error(std::string("glTF: buffer range out of bounds for ") + label);
            return false;
        }

        const unsigned char* data  = buf.data.data() + baseOffset;
        const size_t         count = static_cast<size_t>(accessor.count);

        const size_t stride =
            (view.byteStride > 0)
                ? static_cast<size_t>(view.byteStride)
                : sizeof(float) * 2u;

        if (stride < sizeof(float) * 2u)
        {
            report.error(std::string("glTF: invalid stride for ") + label);
            return false;
        }

        out.resize(count);

        for (size_t i = 0; i < count; ++i)
        {
            const float* pf = reinterpret_cast<const float*>(data + i * stride);
            out[i]          = glm::vec2(pf[0], pf[1]);
        }

        return true;
    }

    static bool readIndices(const tinygltf::Model&    model,
                            const tinygltf::Accessor& accessor,
                            std::vector<uint32_t>&    out,
                            SceneIOReport&            report)
    {
        out.clear();

        if (accessor.type != TINYGLTF_TYPE_SCALAR)
        {
            report.error("glTF: indices accessor must be SCALAR.");
            return false;
        }

        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        {
            report.error("glTF: invalid bufferView for indices.");
            return false;
        }

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
        {
            report.error("glTF: invalid buffer index for indices.");
            return false;
        }

        const tinygltf::Buffer& buf = model.buffers[view.buffer];

        const size_t baseOffset = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(accessor.byteOffset);
        if (baseOffset >= buf.data.size())
        {
            report.error("glTF: buffer range out of bounds for indices.");
            return false;
        }

        const unsigned char* data = buf.data.data() + baseOffset;

        const size_t count    = static_cast<size_t>(accessor.count);
        const size_t elemSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);

        const size_t stride =
            (view.byteStride > 0)
                ? static_cast<size_t>(view.byteStride)
                : elemSize;

        if (elemSize == 0 || stride < elemSize)
        {
            report.error("glTF: invalid stride for indices.");
            return false;
        }

        out.resize(count);

        for (size_t i = 0; i < count; ++i)
        {
            const unsigned char* p = data + i * stride;

            switch (accessor.componentType)
            {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    out[i] = static_cast<uint32_t>(*reinterpret_cast<const uint8_t*>(p));
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    out[i] = static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(p));
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    out[i] = static_cast<uint32_t>(*reinterpret_cast<const uint32_t*>(p));
                    break;
                default:
                    report.error("glTF: unsupported index component type (expected U8/U16/U32).");
                    return false;
            }
        }

        return true;
    }

    static void triangulateStrip(const std::vector<uint32_t>& in, std::vector<uint32_t>& out)
    {
        out.clear();
        if (in.size() < 3)
            return;

        for (size_t i = 2; i < in.size(); ++i)
        {
            const uint32_t a = in[i - 2];
            const uint32_t b = in[i - 1];
            const uint32_t c = in[i];

            if ((i & 1u) == 0u)
            {
                out.push_back(a);
                out.push_back(b);
                out.push_back(c);
            }
            else
            {
                out.push_back(b);
                out.push_back(a);
                out.push_back(c);
            }
        }
    }

    static void triangulateFan(const std::vector<uint32_t>& in, std::vector<uint32_t>& out)
    {
        out.clear();
        if (in.size() < 3)
            return;

        const uint32_t center = in[0];
        for (size_t i = 2; i < in.size(); ++i)
        {
            out.push_back(center);
            out.push_back(in[i - 1]);
            out.push_back(in[i]);
        }
    }

    // ------------------------------------------------------------
    // Texture cache + importer
    // ------------------------------------------------------------
    struct GltfTextureCache
    {
        std::vector<ImageId> texToImage; // glTF texture index -> ImageId
    };

    static ImageId importGltfTextureToImageId(Scene*                       scene,
                                              const tinygltf::Model&       model,
                                              int                          textureIndex,
                                              const std::filesystem::path& baseDir,
                                              GltfTextureCache&            cache,
                                              SceneIOReport&               report)
    {
        if (!scene)
            return kInvalidImageId;

        ImageHandler* ih = scene->imageHandler();
        if (!ih)
            return kInvalidImageId;

        if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
            return kInvalidImageId;

        if (cache.texToImage.empty())
            cache.texToImage.assign(model.textures.size(), kInvalidImageId);
        else if (cache.texToImage.size() != model.textures.size())
            cache.texToImage.resize(model.textures.size(), kInvalidImageId);

        if (cache.texToImage[textureIndex] != kInvalidImageId)
            return cache.texToImage[textureIndex];

        const tinygltf::Texture& tex = model.textures[textureIndex];

        if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size()))
        {
            report.warning("glTF: texture has invalid image source index.");
            return kInvalidImageId;
        }

        const tinygltf::Image& img      = model.images[tex.source];
        const std::string      nameHint = makeName(img.name, tex.source, "Image_");

        // NOTE:
        // We currently pass flipY=true when creating textures. That means we *should not*
        // also flip UVs here unless the rest of your pipeline expects it.
        // If you later decide UVs need flipping, flip them at import (see below).

        // 1) External path URI
        if (!img.uri.empty())
        {
            if (img.uri.rfind("data:", 0) == 0)
            {
                report.warning("glTF: data: URI images not supported yet (skipping): " + nameHint);
                return kInvalidImageId;
            }

            const std::filesystem::path full = baseDir / img.uri;

            const ImageId id = ih->loadFromFile(full, /*flipY=*/true);
            if (id == kInvalidImageId)
            {
                report.warning("glTF: failed to load image file: " + full.string());
                return kInvalidImageId;
            }

            cache.texToImage[textureIndex] = id;
            return id;
        }

        // 2) Embedded image with bufferView (often encoded PNG/JPG in GLB)
        if (img.bufferView >= 0 && img.bufferView < static_cast<int>(model.bufferViews.size()))
        {
            const tinygltf::BufferView& bv = model.bufferViews[img.bufferView];
            if (bv.buffer >= 0 && bv.buffer < static_cast<int>(model.buffers.size()))
            {
                const tinygltf::Buffer& buf = model.buffers[bv.buffer];

                const size_t off  = static_cast<size_t>(bv.byteOffset);
                const size_t size = static_cast<size_t>(bv.byteLength);

                if (off + size <= buf.data.size() && size > 0)
                {
                    const unsigned char* p  = buf.data.data() + off;
                    const ImageId        id = ih->loadFromEncodedMemory(
                        std::span<const unsigned char>(p, size),
                        nameHint,
                        /*flipY=*/true);

                    if (id != kInvalidImageId)
                    {
                        cache.texToImage[textureIndex] = id;
                        return id;
                    }

                    report.warning("glTF: failed to decode embedded image (bufferView): " + nameHint);
                    return kInvalidImageId;
                }
            }
        }

        // 3) Decoded pixels (TinyGLTF may decode for you into img.image + width/height/component)
        if (!img.image.empty() && img.width > 0 && img.height > 0 && img.component > 0)
        {
            const ImageId id =
                ih->createFromRaw(img.image.data(),
                                  img.width,
                                  img.height,
                                  img.component,
                                  nameHint,
                                  /*flipY=*/true);

            if (id != kInvalidImageId)
            {
                cache.texToImage[textureIndex] = id;
                return id;
            }

            report.warning("glTF: failed to create image from decoded pixels: " + nameHint);
            return kInvalidImageId;
        }

        report.warning("glTF: image had no uri/bufferView/pixels: " + nameHint);
        return kInvalidImageId;
    }

    // ------------------------------------------------------------
    // glTF -> Material mapping
    // ------------------------------------------------------------
    static Material::AlphaMode toAlphaMode(const std::string& s)
    {
        if (s == "MASK")
            return Material::AlphaMode::Mask;
        if (s == "BLEND")
            return Material::AlphaMode::Blend;
        return Material::AlphaMode::Opaque;
    }

    static uint32_t resolveMaterialIndex(Scene*                       scene,
                                         const tinygltf::Model&       model,
                                         int                          gltfMatIndex,
                                         const std::filesystem::path& baseDir,
                                         std::vector<uint32_t>&       matCache,
                                         GltfTextureCache&            texCache,
                                         SceneIOReport&               report)
    {
        if (!scene)
            return 0;

        MaterialHandler* mh = scene->materialHandler();
        if (!mh)
            return 0;

        if (gltfMatIndex < 0 || gltfMatIndex >= static_cast<int>(model.materials.size()))
            return 0;

        if (matCache.empty())
            matCache.assign(model.materials.size(), std::numeric_limits<uint32_t>::max());

        if (matCache[gltfMatIndex] != std::numeric_limits<uint32_t>::max())
            return matCache[gltfMatIndex];

        const tinygltf::Material& gm   = model.materials[gltfMatIndex];
        const std::string         name = makeName(gm.name, gltfMatIndex, "Material_");

        const int matId = mh->createMaterial(name);
        if (matId < 0)
        {
            report.warning("glTF: failed to create material: " + name + " (using Default=0)");
            matCache[gltfMatIndex] = 0;
            return 0;
        }

        Material& dst = mh->material(matId);

        // Flags
        dst.alphaMode(toAlphaMode(gm.alphaMode));
        dst.doubleSided(gm.doubleSided);

        if (dst.alphaMode() == Material::AlphaMode::Mask)
        {
            report.info("glTF: material '" + dst.name() + "' alphaMode=MASK alphaCutoff=" + std::to_string(gm.alphaCutoff));
        }

        // Emissive factor
        if (gm.emissiveFactor.size() == 3)
        {
            const glm::vec3 e{
                static_cast<float>(gm.emissiveFactor[0]),
                static_cast<float>(gm.emissiveFactor[1]),
                static_cast<float>(gm.emissiveFactor[2])};
            dst.emissiveColor(e);

            const float maxc = std::max(e.x, std::max(e.y, e.z));
            dst.emissiveIntensity(maxc > 0.0f ? 1.0f : 0.0f);
        }

        // PBR MR
        const tinygltf::PbrMetallicRoughness& pbr = gm.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() == 4)
        {
            const glm::vec3 bc{
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2])};
            dst.baseColor(bc);

            float a = static_cast<float>(pbr.baseColorFactor[3]);
            a       = std::clamp(a, 0.0f, 1.0f);

            if (dst.alphaMode() == Material::AlphaMode::Opaque)
                a = 1.0f;

            dst.opacity(a);
        }

        dst.metallic(static_cast<float>(pbr.metallicFactor));
        dst.roughness(static_cast<float>(pbr.roughnessFactor));

        // Textures
        if (pbr.baseColorTexture.index >= 0)
        {
            const ImageId id = importGltfTextureToImageId(scene, model, pbr.baseColorTexture.index, baseDir, texCache, report);
            if (id != kInvalidImageId)
                dst.baseColorTexture(id);
        }

        ImageId mrId = kInvalidImageId;
        if (pbr.metallicRoughnessTexture.index >= 0)
        {
            mrId = importGltfTextureToImageId(scene, model, pbr.metallicRoughnessTexture.index, baseDir, texCache, report);
            if (mrId != kInvalidImageId)
                dst.mraoTexture(mrId);
        }

        if (gm.occlusionTexture.index >= 0)
        {
            const ImageId aoId = importGltfTextureToImageId(scene, model, gm.occlusionTexture.index, baseDir, texCache, report);

            if (aoId != kInvalidImageId)
            {
                if (mrId == kInvalidImageId)
                {
                    dst.mraoTexture(aoId);
                    report.warning("glTF: material '" + dst.name() + "' has occlusionTexture but no metallicRoughnessTexture; using AO texture in MRAO slot.");
                }
                else if (gm.occlusionTexture.index != pbr.metallicRoughnessTexture.index)
                {
                    report.warning("glTF: material '" + dst.name() + "' has separate occlusionTexture and metallicRoughnessTexture; your Material has one MRAO slot. Keeping metallicRoughnessTexture; AO ignored unless you add a separate slot.");
                }
            }
        }

        if (gm.normalTexture.index >= 0)
        {
            const ImageId id = importGltfTextureToImageId(scene, model, gm.normalTexture.index, baseDir, texCache, report);
            if (id != kInvalidImageId)
                dst.normalTexture(id);
        }

        if (gm.emissiveTexture.index >= 0)
        {
            const ImageId id = importGltfTextureToImageId(scene, model, gm.emissiveTexture.index, baseDir, texCache, report);
            if (id != kInvalidImageId)
                dst.emissiveTexture(id);
        }

        matCache[gltfMatIndex] = static_cast<uint32_t>(matId);
        return static_cast<uint32_t>(matId);
    }

    // ------------------------------------------------------------
    // Optional UV flip helper
    // ------------------------------------------------------------
    static glm::vec2 maybeFlipUv(glm::vec2 uv, bool flipUvY) noexcept
    {
        if (flipUvY)
            uv.y = 1.0f - uv.y;
        return uv;
    }

} // namespace

bool GltfSceneFormat::load(Scene*                       scene,
                           const std::filesystem::path& filePath,
                           const LoadOptions&           options,
                           SceneIOReport&               report)
{
    // ---------------------------------------------------------
    // Basic validation
    // ---------------------------------------------------------
    if (scene == nullptr)
    {
        report.error("GltfSceneFormat::load: scene is null");
        report.status = SceneIOStatus::InvalidScene;
        return false;
    }

    if (!std::filesystem::exists(filePath))
    {
        report.status = SceneIOStatus::FileNotFound;
        report.error("File not found: " + filePath.string());
        return false;
    }

    const std::string ext    = filePath.extension().string();
    const bool        isGltf = (ext == ".gltf" || ext == ".GLTF");
    const bool        isGlb  = (ext == ".glb" || ext == ".GLB");

    if (!isGltf && !isGlb)
    {
        report.status = SceneIOStatus::UnsupportedFormat;
        report.error("Unsupported file extension (expected .gltf or .glb): " + filePath.string());
        return false;
    }

    // ---------------------------------------------------------
    // Merge / replace
    // ---------------------------------------------------------
    if (!options.mergeIntoExisting)
    {
        scene->clear();
    }

    // ---------------------------------------------------------
    // Parse via TinyGLTF
    // ---------------------------------------------------------
    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        err;
    std::string        warn;

    bool ok = false;
    if (isGlb)
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, filePath.string());
    else
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, filePath.string());

    if (!warn.empty())
        report.warning("glTF warning: " + warn);

    if (!ok)
    {
        report.status = SceneIOStatus::ParseError;
        if (!err.empty())
            report.error("glTF error: " + err);
        else
            report.error("glTF parse failed (no error string).");
        return false;
    }

    if (model.scenes.empty())
    {
        report.warning("glTF: file contains no scenes. Nothing to import.");
        report.status = SceneIOStatus::Ok;
        return true;
    }

    // Choose scene
    int sceneIndex = model.defaultScene;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model.scenes.size()))
        sceneIndex = 0;

    const tinygltf::Scene& gltfScene = model.scenes[sceneIndex];

    // Base directory for external image URIs
    const std::filesystem::path baseDir = filePath.parent_path();

    // ---------------------------------------------------------
    // Compute node world matrices for selected glTF scene roots
    // ---------------------------------------------------------
    std::vector<glm::mat4> world;
    world.resize(model.nodes.size(), glm::mat4(1.0f));

    for (int root : gltfScene.nodes)
    {
        buildWorldMatrices(model, root, glm::mat4(1.0f), world);
    }

    // ---------------------------------------------------------
    // Caches
    // ---------------------------------------------------------
    std::vector<uint32_t> matCache;
    GltfTextureCache      texCache;

    // ---------------------------------------------------------
    // Import nodes that reference meshes
    // ---------------------------------------------------------
    int importedMeshCount = 0;

    // If you flip images at load time (we do: flipY=true), usually you do NOT flip UVs.
    // But different pipelines differ. If textures are upside-down, set this to true.
    const bool flipUvY = true;

    for (size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
    {
        const tinygltf::Node& n = model.nodes[nodeIdx];
        if (n.mesh < 0 || n.mesh >= static_cast<int>(model.meshes.size()))
            continue;

        const tinygltf::Mesh& gm = model.meshes[n.mesh];

        const std::string nodeName      = makeName(n.name, static_cast<int>(nodeIdx), "Node_");
        const std::string meshName      = makeName(gm.name, n.mesh, "Mesh_");
        const std::string sceneMeshName = nodeName + "_" + meshName;

        SceneMesh* sceneMesh = scene->createSceneMesh(sceneMeshName);
        if (!sceneMesh)
        {
            report.error("glTF: scene->createSceneMesh failed for: " + sceneMeshName);
            report.status = SceneIOStatus::InvalidScene;
            return false;
        }

        SysMesh* mesh = sceneMesh->sysMesh();
        if (!mesh)
        {
            report.error("glTF: SceneMesh::sysMesh returned null for: " + sceneMeshName);
            report.status = SceneIOStatus::InvalidScene;
            return false;
        }

        // Create face-varying maps (match your OBJ loader convention)
        const int normMap = mesh->map_create(0, 0, 3);
        const int texMap  = mesh->map_create(1, 0, 2);

        // Bake node transform into vertices/normals
        const glm::mat4 M = world[nodeIdx];
        const glm::mat3 N = glm::mat3(glm::inverseTranspose(M));

        for (size_t primIdx = 0; primIdx < gm.primitives.size(); ++primIdx)
        {
            const tinygltf::Primitive& prim = gm.primitives[primIdx];

            // POSITION required
            auto itPos = prim.attributes.find("POSITION");
            if (itPos == prim.attributes.end())
            {
                report.warning("glTF: primitive missing POSITION, skipping. (" + sceneMeshName + ")");
                continue;
            }

            const int posAccIndex = itPos->second;
            if (posAccIndex < 0 || posAccIndex >= static_cast<int>(model.accessors.size()))
            {
                report.warning("glTF: invalid POSITION accessor, skipping primitive. (" + sceneMeshName + ")");
                continue;
            }

            std::vector<glm::vec3> positions;
            if (!readAccessorVec3Float(model, model.accessors[posAccIndex], positions, report, "POSITION"))
                return false;

            // Optional NORMAL
            std::vector<glm::vec3> normals;
            {
                auto itNrm = prim.attributes.find("NORMAL");
                if (itNrm != prim.attributes.end())
                {
                    const int nAccIndex = itNrm->second;
                    if (nAccIndex >= 0 && nAccIndex < static_cast<int>(model.accessors.size()))
                    {
                        if (!readAccessorVec3Float(model, model.accessors[nAccIndex], normals, report, "NORMAL"))
                            return false;

                        if (normals.size() != positions.size())
                        {
                            report.warning("glTF: NORMAL count != POSITION count. Ignoring normals for this primitive.");
                            normals.clear();
                        }
                    }
                }
            }

            // Optional TEXCOORD_0
            std::vector<glm::vec2> uvs;
            {
                auto itUv = prim.attributes.find("TEXCOORD_0");
                if (itUv != prim.attributes.end())
                {
                    const int uvAccIndex = itUv->second;
                    if (uvAccIndex >= 0 && uvAccIndex < static_cast<int>(model.accessors.size()))
                    {
                        if (!readAccessorVec2Float(model, model.accessors[uvAccIndex], uvs, report, "TEXCOORD_0"))
                            return false;

                        if (uvs.size() != positions.size())
                        {
                            report.warning("glTF: TEXCOORD_0 count != POSITION count. Ignoring UVs for this primitive.");
                            uvs.clear();
                        }
                    }
                }
            }

            // Indices (optional)
            std::vector<uint32_t> indices;
            if (prim.indices >= 0)
            {
                if (prim.indices >= static_cast<int>(model.accessors.size()))
                {
                    report.warning("glTF: invalid indices accessor, skipping primitive. (" + sceneMeshName + ")");
                    continue;
                }

                if (!readIndices(model, model.accessors[prim.indices], indices, report))
                    return false;
            }
            else
            {
                indices.resize(positions.size());
                for (size_t i = 0; i < indices.size(); ++i)
                    indices[i] = static_cast<uint32_t>(i);
            }

            // Convert to triangles
            std::vector<uint32_t> tri;
            switch (prim.mode)
            {
                case TINYGLTF_MODE_TRIANGLES:
                    tri = indices;
                    break;
                case TINYGLTF_MODE_TRIANGLE_STRIP:
                    triangulateStrip(indices, tri);
                    break;
                case TINYGLTF_MODE_TRIANGLE_FAN:
                    triangulateFan(indices, tri);
                    break;
                default:
                    report.warning("glTF: unsupported primitive mode (not triangles/strip/fan). Skipping primitive.");
                    continue;
            }

            if (tri.size() < 3 || (tri.size() % 3u) != 0u)
                continue;

            // Material
            uint32_t matIndex = 0;
            if (prim.material >= 0)
                matIndex = resolveMaterialIndex(scene, model, prim.material, baseDir, matCache, texCache, report);

            // Create SysMesh verts for this primitive (no dedupe; simple & correct)
            std::vector<int32_t> vRemap;
            vRemap.resize(positions.size(), -1);

            for (size_t i = 0; i < positions.size(); ++i)
            {
                const glm::vec4 p4 = M * glm::vec4(positions[i], 1.0f);
                const glm::vec3 p  = glm::vec3(p4.x, p4.y, p4.z);
                vRemap[i]          = mesh->create_vert(p);
            }

            const auto safeNorm = [](const glm::vec3& v) -> glm::vec3 {
                const float len2 = glm::dot(v, v);
                if (len2 <= 0.0f)
                    return glm::vec3(0.0f, 1.0f, 0.0f);
                return glm::normalize(v);
            };

            // Emit triangles as polys, attach face-varying normal/uv (if present)
            for (size_t t = 0; t < tri.size(); t += 3u)
            {
                const uint32_t i0 = tri[t + 0u];
                const uint32_t i1 = tri[t + 1u];
                const uint32_t i2 = tri[t + 2u];

                if (i0 >= vRemap.size() || i1 >= vRemap.size() || i2 >= vRemap.size())
                {
                    report.warning("glTF: triangle index out of range. Skipping triangle.");
                    continue;
                }

                SysPolyVerts pv;
                pv.push_back(vRemap[i0]);
                pv.push_back(vRemap[i1]);
                pv.push_back(vRemap[i2]);

                const int poly = mesh->create_poly(pv, static_cast<int>(matIndex));
                if (poly < 0)
                    continue;

                if (normMap != -1 && !normals.empty())
                {
                    SysPolyVerts pn;
                    pn.reserve(3);

                    glm::vec3 n0 = safeNorm(N * normals[i0]);
                    glm::vec3 n1 = safeNorm(N * normals[i1]);
                    glm::vec3 n2 = safeNorm(N * normals[i2]);

                    pn.push_back(mesh->map_create_vert(normMap, glm::value_ptr(n0)));
                    pn.push_back(mesh->map_create_vert(normMap, glm::value_ptr(n1)));
                    pn.push_back(mesh->map_create_vert(normMap, glm::value_ptr(n2)));

                    mesh->map_create_poly(normMap, poly, pn);
                }

                if (texMap != -1 && !uvs.empty())
                {
                    SysPolyVerts pt;
                    pt.reserve(3);

                    const glm::vec2 uv0 = maybeFlipUv(uvs[i0], flipUvY);
                    const glm::vec2 uv1 = maybeFlipUv(uvs[i1], flipUvY);
                    const glm::vec2 uv2 = maybeFlipUv(uvs[i2], flipUvY);

                    pt.push_back(mesh->map_create_vert(texMap, glm::value_ptr(uv0)));
                    pt.push_back(mesh->map_create_vert(texMap, glm::value_ptr(uv1)));
                    pt.push_back(mesh->map_create_vert(texMap, glm::value_ptr(uv2)));

                    mesh->map_create_poly(texMap, poly, pt);
                }
            }
        }

        ++importedMeshCount;
    }

    if (importedMeshCount == 0)
        report.warning("glTF: no meshes found to import.");
    else
        report.info("glTF: imported " + std::to_string(importedMeshCount) + " scene meshes.");

    report.status = SceneIOStatus::Ok;
    return !report.hasErrors();
}

bool GltfSceneFormat::save(const Scene* /*scene*/,
                           const std::filesystem::path& /*filePath*/,
                           const SaveOptions& /*options*/,
                           SceneIOReport& report)
{
    report.status = SceneIOStatus::UnsupportedFormat;
    report.error("GltfSceneFormat::save: saving is not supported yet (import-only).");
    return false;
}
