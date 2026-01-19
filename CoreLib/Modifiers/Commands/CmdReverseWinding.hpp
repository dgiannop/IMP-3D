#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Reverse polygon winding for selected polygons (or all polygons if none selected).
 *
 * - Reverses SysMesh polygon vertex order.
 * - Also reverses face-varying UVs (map id 1) and normals (map id 0) per-corner ordering.
 * - Preserves polygon material IDs.
 *
 * Note:
 * - SysMesh has no in-place poly-vert edit API, so this is implemented as remove+recreate.
 * - With HoleList reuse, the same poly id will often be re-used, but it is not guaranteed.
 */
class CmdReverseWinding final : public Command
{
public:
    bool execute(Scene* scene) override;
};
