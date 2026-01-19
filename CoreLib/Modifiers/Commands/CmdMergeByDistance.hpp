// CmdMergeByDistance.hpp
#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Weld vertices that are within a small distance (auto-weld).
 *
 * Selection behavior:
 *  - If vertices are selected: weld those vertices (and any connected polys will update).
 *  - Else if polygons are selected: weld vertices of those polygons.
 *  - Else: weld all vertices.
 *
 * Maps:
 *  - DOES NOT weld map verts (UVs/normals remain face-varying and seams are preserved).
 *  - When a polygon corner is removed due to degeneracy cleanup, mapped polygon corners
 *    are removed in the same way and the mapped polygon is recreated referencing the
 *    original map-vert IDs.
 */
class CmdMergeByDistance final : public Command
{
public:
    bool execute(Scene* scene) override;
};
