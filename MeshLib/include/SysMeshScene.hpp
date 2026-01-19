#pragma once

#include <vector>

#include "History.hpp"

class SysMesh;

/**
 * @brief Backend-agnostic scene interface operating directly on SysMesh objects.
 *
 * This class provides undo/redo support and standardized access to the collection
 * of meshes in the scene, without relying on high-level wrappers.
 *
 * It is intended for tool-level logic and core mesh manipulation.
 *
 * For wrapper-based support (e.g., Mesh/MeshVert/MeshEdge), see MeshScene (future).
 */
class SysMeshScene
{
public:
    SysMeshScene();
    virtual ~SysMeshScene();

    /**
     * @brief Returns the global scene-level undo/redo stack.
     */
    History& history() { return m_sceneHistory; }

    /**
     * @brief Commits all pending mesh edits as a single undoable action.
     *
     * For each selected mesh, this releases its current history action
     * and wraps them all into a scene-wide History transaction.
     */
    void commitMeshChanges();

    /**
     * @brief Aborts (undoes) all uncommitted changes on selected meshes.
     */
    void abortMeshChanges();

    /**
     * @brief Returns true if there are uncommitted mesh edits (per-mesh histories)
     *        that have not yet been wrapped into the scene history.
     *
     * Typical use: if user presses Undo/Redo while a tool preview is active,
     * cancel/abort the preview first instead of undoing the previous committed step.
     */
    [[nodiscard]] bool hasPendingMeshChanges() const;

    /**
     * @return All SysMesh instances in the scene.
     */
    virtual std::vector<SysMesh*> meshes() const = 0;

    /**
     * @return Subset of meshes currently selected by the user.
     */
    virtual std::vector<SysMesh*> selectedMeshes() const = 0;

    /**
     * @return Subset of meshes that are currently visible in the viewport.
     */
    virtual std::vector<SysMesh*> visibleMeshes() const = 0;

    /**
     * @return Subset of meshes that are both selected and visible.
     *         Used by tools to operate only on user-targeted geometry.
     */
    virtual std::vector<SysMesh*> activeMeshes() const = 0;

private:
    History m_sceneHistory; ///< History stack that tracks scene-wide undo blocks
};
