#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "SysCounter.hpp"

class Material;
class MaterialHandler;
class Scene;

/**
 * @file MaterialEditor.hpp
 * @brief Core-facing facade for querying and editing scene materials.
 *
 * The MaterialEditor provides a stable, UI-friendly API for material enumeration,
 * creation, and lookup, without exposing Scene internals such as MaterialHandler
 * containers or implementation details.
 *
 * Design goals:
 *  - Keep Core's public API small (avoid per-property Core setters).
 *  - Avoid giving UI direct access to Scene/MaterialHandler containers.
 *  - Provide a single place to later enforce policies (undo/redo, validation,
 *    dedup rules, rename rules, etc.) without rewriting UI code.
 *
 * Notes:
 *  - Material IDs are currently indices into MaterialHandler's vector.
 *  - This interface can remain stable even if we later move to stable IDs,
 *    freelists, or pooled storage.
 */
class MaterialEditor final
{
public:
    /**
     * @brief Lightweight material list entry (for UI lists).
     *
     * The UI usually needs only a stable id + display name to populate lists.
     * Any additional properties can be queried via material(id).
     */
    struct Entry
    {
        int32_t     id   = -1;
        std::string name = {};
    };

public:
    /**
     * @brief Construct editor bound to a Scene.
     * @param scene Owning scene; may be null.
     */
    explicit MaterialEditor(Scene* scene) noexcept;

    MaterialEditor(const MaterialEditor&)            = delete;
    MaterialEditor& operator=(const MaterialEditor&) = delete;

    MaterialEditor(MaterialEditor&&)            = delete;
    MaterialEditor& operator=(MaterialEditor&&) = delete;

    ~MaterialEditor() noexcept = default;

    /**
     * @brief Rebind the editor to a different scene.
     * @param scene New scene; may be null.
     */
    void setScene(Scene* scene) noexcept;

    /**
     * @brief Get the current bound scene.
     */
    [[nodiscard]] Scene* scene() const noexcept;

    // ---------------------------------------------------------------------
    // Enumeration / Lookup
    // ---------------------------------------------------------------------

    /**
     * @brief Enumerate all materials as lightweight entries.
     *
     * This is intended for quickly populating UI lists.
     * @return Vector of {id, name} for all materials.
     */
    [[nodiscard]] std::vector<Entry> list() const;

    /**
     * @brief Resolve a material pointer by id (read-only).
     * @param id Material id/index.
     * @return Pointer to material or nullptr if invalid.
     */
    [[nodiscard]] const Material* material(int32_t id) const noexcept;

    /**
     * @brief Resolve a material pointer by id (mutable).
     * @param id Material id/index.
     * @return Pointer to material or nullptr if invalid.
     */
    [[nodiscard]] Material* material(int32_t id) noexcept;

    /**
     * @brief Find a material id by name (case-insensitive).
     * @param name Material name.
     * @return Material id, or -1 if not found.
     */
    [[nodiscard]] int32_t findByName(std::string_view name) const;

    // ---------------------------------------------------------------------
    // Creation
    // ---------------------------------------------------------------------

    /**
     * @brief Create a new material or return an existing id.
     *
     * Uses MaterialHandler's createMaterial() behavior:
     *  - If name is not the default and matches an existing material
     *    case-insensitively, returns the existing id.
     *  - Otherwise creates a new one (suffixing if necessary).
     *
     * @param name Desired material name.
     * @return Material id (>= 0) on success, or -1 if no scene/handler.
     */
    [[nodiscard]] int32_t createOrGet(std::string_view name);

    // ---------------------------------------------------------------------
    // Change tracking
    // ---------------------------------------------------------------------

    /**
     * @brief Change counter for the material library.
     *
     * UI can monitor this to refresh list / properties when materials change.
     * @return Counter ptr or empty if no scene/handler.
     */
    [[nodiscard]] SysCounterPtr changeCounter() const;

private:
    [[nodiscard]] const MaterialHandler* handler() const noexcept;
    [[nodiscard]] MaterialHandler*       handler() noexcept;

private:
    Scene* m_scene = nullptr;
};
