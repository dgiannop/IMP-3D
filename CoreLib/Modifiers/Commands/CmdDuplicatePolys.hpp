#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Duplicate selected polygons (or all polygons if none selected).
 *
 * Duplicates:
 *  - base geometry (verts + polys)
 *  - per-poly material id
 *  - face-varying UVs from map id 1 (if present)
 *  - face-varying normals from map id 0 (if present)
 *
 * Selection after:
 *  - selects the newly created polygons
 */
class CmdDuplicatePolys final : public Command
{
public:
    bool execute(Scene* scene) override;
};
