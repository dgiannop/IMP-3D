#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "ItemFactory.hpp"
#include "SceneFormat.hpp"

class Scene;

/**
 * @brief Application-level document wrapper for a Scene.
 *
 * CoreDocument implements "document semantics" without any UI dependency:
 *  - Tracks a current document path (native project file)
 *  - Tracks unsaved changes via Scene change counter (with a safe fallback flag)
 *  - Delegates load/save to registered SceneFormat handlers via ItemFactory
 *
 * @note CoreDocument does NOT own the Scene; Core owns it.
 *
 * Policy (current project behavior):
 *  - Native project format is ".imp"
 *  - save() writes to the current document path (must exist and must be ".imp")
 *  - saveAs() writes to a new native path (forces ".imp") and updates the document path
 *  - exportFile() writes to other formats without changing document path or save-state
 *  - openFile() replaces the scene (unless options.mergeIntoExisting=true)
 *  - importFile() merges into the existing scene and does NOT change document path
 *
 * UI responsibilities:
 *  - requestNew()/requestExit() are gates only; if false, UI shows "Save/Discard/Cancel"
 *  - If save() returns false due to missing path, UI should show a Save As dialog
 */
class CoreDocument
{
public:
    /**
     * @brief Construct a document wrapper for an existing Scene (non-owning).
     * @param owner Non-owning Scene pointer (must remain valid for lifetime of CoreDocument).
     */
    explicit CoreDocument(Scene* owner) noexcept;

    CoreDocument(const CoreDocument&)            = delete;
    CoreDocument& operator=(const CoreDocument&) = delete;

    // ------------------------------------------------------------
    // Format registry (Config.cpp registers formats here)
    // ------------------------------------------------------------

    /**
     * @brief Factory used to register and instantiate SceneFormat handlers by extension.
     *
     * Example registration:
     * @code
     * factory.registerItem(".imp", factory.createItemType<ImpSceneFormat>);
     * factory.registerItem(".obj", factory.createItemType<ObjSceneFormat>);
     * @endcode
     */
    ItemFactory<SceneFormat>& formatFactory() noexcept;

    // ------------------------------------------------------------
    // Document lifecycle gates (NO UI inside these)
    // ------------------------------------------------------------

    /**
     * @brief Gate for "New" operation.
     * @return True if it is safe to create a new document without prompting (no unsaved changes).
     */
    [[nodiscard]] bool requestNew() const noexcept;

    /**
     * @brief Gate for "Exit" operation.
     * @return True if it is safe to exit without prompting (no unsaved changes).
     */
    [[nodiscard]] bool requestExit() const noexcept;

    // ------------------------------------------------------------
    // Dirty tracking
    // ------------------------------------------------------------

    /**
     * @brief Check whether the document has unsaved changes.
     *
     * Uses Scene::changeCounter() if available; otherwise uses an internal fallback flag
     * that is set after operations likely to modify the scene (e.g. import).
     */
    [[nodiscard]] bool hasUnsavedChanges() const noexcept;

    /**
     * @brief Reset "saved state" snapshot to the current scene state.
     *
     * Call this after successful save or load into the current document state.
     */
    void resetSaveState() noexcept;

    // ------------------------------------------------------------
    // Path
    // ------------------------------------------------------------

    /**
     * @brief Whether a document path is currently set.
     */
    [[nodiscard]] bool hasFilePath() const noexcept;

    /**
     * @brief Current document path (native project file).
     *
     * @return Path reference, or an empty path if no path is set.
     */
    [[nodiscard]] const std::filesystem::path& filePath() const noexcept;

    /**
     * @brief Clears the current document path (document becomes "Untitled").
     */
    void clearFilePath() noexcept;

    // ------------------------------------------------------------
    // Actions (the "just do it" operations)
    // ------------------------------------------------------------

    /**
     * @brief Create a new empty document.
     *
     * Clears the Scene, clears the document path, and resets the saved-state snapshot.
     * This function does not prompt; caller should use requestNew() gating first.
     *
     * @return True if successful.
     */
    bool newFile() noexcept;

    /**
     * @brief Open a file into the scene.
     *
     * Delegates to the appropriate SceneFormat based on file extension.
     * On success, updates the document path to the opened file.
     */
    bool openFile(const std::filesystem::path& path, const LoadOptions& options, SceneIOReport* report = nullptr);

    /**
     * @brief Import a file into the current scene (merge).
     *
     * Delegates to the appropriate SceneFormat based on file extension.
     * On success, does NOT change the current document path.
     */
    bool importFile(const std::filesystem::path& path, const LoadOptions& options, SceneIOReport* report = nullptr);

    /**
     * @brief Save the current document to its existing native ".imp" path.
     *
     * @return True if saved. False if no path is set, not native, or write failed.
     */
    bool save(const SaveOptions& options = {}, SceneIOReport* report = nullptr);

    /**
     * @brief Save the document to a new native ".imp" path and update document path.
     *
     * The extension is forced to ".imp" to enforce native project storage.
     */
    bool saveAs(const std::filesystem::path& path, const SaveOptions& options = {}, SceneIOReport* report = nullptr);

    /**
     * @brief Export the scene to a non-native format (OBJ, glTF, ...).
     *
     * Does NOT modify document path or saved-state snapshot.
     */
    bool exportFile(const std::filesystem::path& path, const SaveOptions& options = {}, SceneIOReport* report = nullptr) const;

private:
    Scene*                               m_scene = nullptr; // non-owning
    std::optional<std::filesystem::path> m_path  = {};

    uint64_t     m_savedCounter  = 0;
    mutable bool m_dirtyFallback = false;

    ItemFactory<SceneFormat> m_formatFactory;

private:
    [[nodiscard]] uint64_t currentCounter() const noexcept;
    void                   markDirtyFallback() const noexcept;

    [[nodiscard]] static std::string extensionLower_(const std::filesystem::path& path);
    [[nodiscard]] static bool        isNativeImp_(const std::filesystem::path& path) noexcept;

    [[nodiscard]] std::unique_ptr<SceneFormat> createFormatForPath_(const std::filesystem::path& path) const;
};
