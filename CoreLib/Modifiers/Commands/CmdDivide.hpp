// CmdDivide.hpp
#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Subdivide selected polygons (flat, 1-step), preserving maps (UVs, normals, etc.).
 *
 * Behavior:
 *  - If polygons are selected: subdivide those polygons.
 *  - Else if edges are selected: subdivide the polygons adjacent to those edges.
 *
 * For each subdivided polygon (n-gon), inserts:
 *  - one midpoint vertex per polygon edge (shared in geometry across adjacent subdivided polys)
 *  - one center vertex per polygon
 *
 * Replaces the original polygon with n quads:
 *  - quad i: [v_i, mid(i), center, mid(i-1)]
 *
 * Maps:
 *  - For every existing map ID in a small probed range, per-corner map verts are preserved by:
 *      - midpoint = lerp(endpoints)
 *      - center   = average(all corners)
 *    (Maps remain face-varying; map verts are created per new face-corner.)
 */
class CmdDivide final : public Command
{
public:
    bool execute(Scene* scene) override;
};
