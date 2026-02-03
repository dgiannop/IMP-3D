//=============================================================================
// CmdConnect.hpp
//=============================================================================
#pragma once

#include "Command.hpp"

/**
 * @class CmdConnect
 * @brief Connect selected edges by inserting a cut inside adjacent quads.
 *
 * Current scope (command version):
 * - Uses selected edges (or all edges if none selected)
 * - For each selected edge, visits its adjacent polygons
 * - If the adjacent polygon is a quad:
 *   - split the selected edge at t=0.5
 *   - split the opposite edge at t=0.5
 *   - split the quad by connecting the two new vertices
 *
 * Notes:
 * - This is the “local professional core” (quad split) without full strip traversal.
 * - Map propagation is preserved for probed maps (IDs 0..15) when map polys are 1:1 with poly corners.
 */
class CmdConnect final : public Command
{
public:
    CmdConnect()           = default;
    ~CmdConnect() override = default;

    bool execute(class Scene* scene) override;
};
