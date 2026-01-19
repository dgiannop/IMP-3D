#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Scene;

/**
 * @brief Status code for scene load/save operations.
 */
enum class SceneIOStatus
{
    Ok,
    FileNotFound,
    UnsupportedFormat,
    ParseError,
    WriteError,
    Cancelled,
    InvalidScene
};

/**
 * @brief Single informational/warning/error message produced during IO.
 */
struct SceneIOMessage
{
    enum class Type
    {
        Info,
        Warning,
        Error
    };

    Type        type{};
    std::string text;
};

/**
 * @brief Aggregate report for a load/save operation.
 *
 * Collects messages and a final status that can be shown in the UI.
 */
struct SceneIOReport
{
    SceneIOStatus               status{ SceneIOStatus::Ok };
    std::vector<SceneIOMessage> messages{};

    void info(std::string msg)
    {
        messages.push_back(SceneIOMessage{ SceneIOMessage::Type::Info, std::move(msg) });
    }

    void warning(std::string msg)
    {
        messages.push_back(SceneIOMessage{ SceneIOMessage::Type::Warning, std::move(msg) });
    }

    void error(std::string msg)
    {
        messages.push_back(SceneIOMessage{ SceneIOMessage::Type::Error, std::move(msg) });
        if (status == SceneIOStatus::Ok)
        {
            status = SceneIOStatus::ParseError;
        }
    }

    [[nodiscard]] bool hasErrors() const
    {
        for (const SceneIOMessage& m : messages)
        {
            if (m.type == SceneIOMessage::Type::Error)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasWarnings() const
    {
        for (const SceneIOMessage& m : messages)
        {
            if (m.type == SceneIOMessage::Type::Warning)
            {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief Load-time options (import vs merge, etc).
 */
struct LoadOptions
{
    bool mergeIntoExisting = false;
    bool triangulate       = false;
};

/**
 * @brief Save-time options (selected-only, compression, etc).
 */
struct SaveOptions
{
    bool selectedOnly   = false;
    bool compressNative = false;
    bool triangulate    = false;
};

/**
 * @brief Base class for a scene file format (OBJ, IMP, glTF, ...).
 *
 * Implementations know how to load/save a Scene from/to a specific file type.
 *
 * Typical derived classes:
 *  - ObjSceneFormat
 *  - ImpSceneFormat (native)
 *  - GltfSceneFormat
 */
class SceneFormat
{
public:
    virtual ~SceneFormat() = default;

    /**
     * @brief Human-readable format name, e.g. "Wavefront OBJ", "IMP3D Native".
     */
    virtual std::string_view formatName() const noexcept = 0;

    /**
     * @brief Primary file extension handled by this format, e.g. ".obj".
     *
     * Used as the key for ItemFactory registration.
     */
    virtual std::string_view extension() const noexcept = 0;

    /**
     * @brief Load a scene from file.
     *
     * @param scene     Target scene to populate or merge into.
     * @param filePath  Path of the file to load.
     * @param options   Load options (merge, etc.).
     * @param report    Report to populate with messages and final status.
     * @return true on success, false on failure.
     */
    virtual bool load(Scene* scene, const std::filesystem::path& filePath, const LoadOptions& options, SceneIOReport& report) = 0;

    /**
     * @brief Save a scene to file.
     *
     * @param scene     Scene to serialize.
     * @param filePath  Path of the file to write.
     * @param options   Save options.
     * @param report    Report to populate with messages and final status.
     * @return true on success, false on failure.
     */
    virtual bool save(const Scene* scene, const std::filesystem::path& filePath, const SaveOptions& options, SceneIOReport& report) = 0;

    /**
     * @brief Whether this format supports saving.
     *
     * Some formats might be import-only.
     */
    [[nodiscard]] virtual bool supportsSave() const noexcept
    {
        return true;
    }
};
