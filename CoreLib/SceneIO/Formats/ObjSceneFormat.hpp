#pragma once

#include "SceneFormat.hpp"

/**
 * @brief Wavefront OBJ scene format loader/saver.
 */
class ObjSceneFormat : public SceneFormat
{
public:
    ObjSceneFormat()           = default;
    ~ObjSceneFormat() override = default;

    [[nodiscard]] std::string_view formatName() const noexcept override
    {
        return "Wavefront OBJ";
    }

    [[nodiscard]] std::string_view extension() const noexcept override
    {
        return ".obj";
    }

    bool load(Scene*                       scene,
              const std::filesystem::path& filePath,
              const LoadOptions&           options,
              SceneIOReport&               report) override;

    bool save(const Scene*                 scene,
              const std::filesystem::path& filePath,
              const SaveOptions&           options,
              SceneIOReport&               report) override;

private:
    bool loadMaterialLibrary(Scene* scene, const std::filesystem::path& mtlFile);
    bool saveMaterialLibrary(const Scene* scene, const std::filesystem::path& mtlFile);
};
