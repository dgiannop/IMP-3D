// CmdTriangulate.hpp
#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Triangulate selected polygons (fan triangulation), preserving maps (UVs, normals, etc.).
 *
 * Behavior:
 *  - If polygons are selected: triangulate those polygons.
 *  - Else if edges are selected: triangulate the polygons adjacent to those edges.
 *  - Else if verts are selected: triangulate adjacent polygons.
 *  - Else: triangulate all polygons.
 *
 * Triangulation:
 *  - For an n-gon [v0 v1 ... v(n-1)], creates triangles:
 *      (v0, vj, v(j+1)) for j = 1..n-2
 *
 * Maps:
 *  - For every existing map ID in a small probed range, per-corner map verts are preserved by:
 *      - triangle corner map verts reuse the original polygon corner map verts
 *    (Maps remain face-varying; no new map verts are created.)
 */
class CmdTriangulate final : public Command
{
public:
    bool execute(Scene* scene) override;
};
