#pragma once

#include <filesystem>
#include <string_view>

#include "SceneFormat.hpp"

class Scene;

/**
 * @brief Native IMP3D scene format (.imp).
 *
 * v1: meshes + transforms + raw geometry
 * v2: + face-varying maps (normals / UVs)
 * v3: + materials (PBR) + images (external path or embedded base64) + lights
 *
 * Extensible by adding new top-level blocks.
 */
class ImpSceneFormat final : public SceneFormat
{
public:
    std::string_view formatName() const noexcept override { return "IMP3D Native"; }
    std::string_view extension() const noexcept override { return ".imp"; }

    bool load(Scene*                       scene,
              const std::filesystem::path& filePath,
              const LoadOptions&           options,
              SceneIOReport&               report) override;

    bool save(const Scene*                 scene,
              const std::filesystem::path& filePath,
              const SaveOptions&           options,
              SceneIOReport&               report) override;
};
