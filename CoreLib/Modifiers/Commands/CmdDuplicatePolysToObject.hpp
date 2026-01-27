#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Duplicate polygons into new SceneMesh objects.
 *
 * Scene-wide selection rule:
 *  - If ANY source mesh has selected polys: duplicate ONLY selected polys per mesh
 *    (meshes with no selected polys are skipped).
 *  - If NO mesh has selected polys: duplicate ALL polys per mesh.
 *
 * Copies:
 *  - vertex positions
 *  - polygons (+ material id)
 *  - face-varying UVs (mapId=1, dim=2) and normals (mapId=0, dim=3) if present on source
 *
 * Selection after:
 *  - Clears component selection on all meshes
 *  - Selects all newly created polys on each new mesh
 */
class CmdDuplicatePolysToObject final : public Command
{
public:
    bool execute(Scene* scene) override;
};
