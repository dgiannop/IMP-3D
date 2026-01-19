#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Smooth normals for selected polys (or all polys if none selected).
 *
 * Writes face-varying normals into normal map id 0 (dim=3).
 * Does not modify topology, UVs, or materials.
 */
class CmdSmoothNormals final : public Command
{
public:
    bool execute(Scene* scene) override;
};
