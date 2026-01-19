#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Center selected geometry (or whole mesh if nothing selected) at world origin.
 *
 * Behavior:
 *  - If mesh has selected verts: center those
 *  - Else if mesh has selected polys: center verts of those polys
 *  - Else: center entire mesh
 *
 * Uses AABB center (not average centroid).
 */
class CmdCenter final : public Command
{
public:
    bool execute(Scene* scene) override;
};
