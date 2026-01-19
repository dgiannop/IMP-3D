#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Move selected geometry (or whole mesh if nothing selected) so its lowest point rests on the ground plane.
 *
 * Default behavior:
 *  - Up axis: +Y
 *  - Ground plane: Y = 0
 *  - If mesh has selected verts: use those verts only
 *  - Else if mesh has selected polys: use verts of those polys
 *  - Else: use all verts of the mesh
 *
 * Translation is applied by moving vertices (SysMesh::move_vert) so undo works.
 */
class CmdRestOnGround final : public Command
{
public:
    bool execute(Scene* scene) override;
};
