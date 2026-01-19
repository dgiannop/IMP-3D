#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Flip normals for selected polygons (or all polygons if none selected).
 *
 * - Does NOT change polygon winding.
 * - Operates on normal map id 0 (dim==3) when present.
 * - Preserves face-varying behavior by rewriting per-poly normal map verts.
 */
class CmdFlipNormals final : public Command
{
public:
    bool execute(Scene* scene) override;
};
