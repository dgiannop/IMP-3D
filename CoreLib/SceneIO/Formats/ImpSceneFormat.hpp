#pragma once

#include <filesystem>
#include <string_view>

#include "SceneFormat.hpp"

class Scene;

/**
 * @brief Native IMP3D scene format (.imp).
 *
 * v1:
 *  - Multiple meshes
 *  - Per-mesh name
 *  - Per-mesh transform (4x4)
 *  - Per-mesh subdivision level
 *  - Raw SysMesh geometry: verts + n-gon polys
 *
 * Extensible by adding new keywords/blocks.
 */
class ImpSceneFormat final : public SceneFormat
{
public:
    std::string_view formatName() const noexcept override
    {
        return "IMP3D Native";
    }
    std::string_view extension() const noexcept override
    {
        return ".imp";
    }

    bool load(Scene* scene, const std::filesystem::path& filePath, const LoadOptions& options, SceneIOReport& report) override;
    bool save(const Scene* scene, const std::filesystem::path& filePath, const SaveOptions& options, SceneIOReport& report) override;
};
