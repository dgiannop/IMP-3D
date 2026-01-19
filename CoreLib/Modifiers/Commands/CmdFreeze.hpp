// CmdFreeze.hpp
#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Bake (freeze) the current OpenSubdiv result back into the SysMesh.
 *
 * Produces a new triangulated base mesh matching the current subdiv level, preserving:
 *  - positions
 *  - face-varying UVs (mapId=1) if present
 *  - normals (as a 3D map) if an existing normals map is found
 *  - materials (face-uniform), copied per baked triangle
 *
 * Notes:
 *  - This replaces the mesh topology.
 *  - For robustness, output is triangles (matches evaluator triangle buffers).
 *  - Creases: can be added later (either bake-only, or preserve crease metadata).
 */
class CmdFreeze final : public Command
{
public:
    bool execute(Scene* scene) override;
};
