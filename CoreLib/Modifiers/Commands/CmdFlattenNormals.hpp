#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Flatten (hard) normals for selected polys (or all polys if none selected).
 *
 * Writes per-face flat normals into normal map id 0 (dim=3) as face-varying normals.
 */
class CmdFlattenNormals final : public Command
{
public:
    bool execute(Scene* scene) override;
};
