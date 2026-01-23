#pragma once

#include "SceneFormat.hpp"

/**
 * @brief glTF 2.0 scene format loader (TinyGLTF).
 *
 * Import-only for now (supports .gltf and .glb).
 */
class GltfSceneFormat : public SceneFormat
{
public:
    GltfSceneFormat()           = default;
    ~GltfSceneFormat() override = default;

    [[nodiscard]] std::string_view formatName() const noexcept override
    {
        return "glTF 2.0";
    }

    [[nodiscard]] std::string_view extension() const noexcept override
    {
        // Primary registration key. load() accepts both .gltf and .glb.
        return ".gltf";
    }

    bool load(Scene*                       scene,
              const std::filesystem::path& filePath,
              const LoadOptions&           options,
              SceneIOReport&               report) override;

    bool save(const Scene*                 scene,
              const std::filesystem::path& filePath,
              const SaveOptions&           options,
              SceneIOReport&               report) override;

    [[nodiscard]] bool supportsSave() const noexcept override
    {
        return false;
    }
};
