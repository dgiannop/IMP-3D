//=============================================================================
// CmdConnect.hpp
//=============================================================================
#pragma once

#include "Command.hpp"

/**
 * @class CmdConnect
 * @brief Connect selected edges by inserting a cut inside adjacent quads.
 *
 * Fix: The previous implementation split edges *locally per polygon*, producing
 * duplicate midpoints and broken adjacency (loops could not traverse).
 *
 * This version:
 * - Creates exactly ONE midpoint vertex per geometric edge (global cache)
 * - Ensures ALL adjacent polys that share that edge are updated to use that same midpoint
 *   (prevents T-junctions / fake connectivity)
 * - Then, for each affected quad, also splits its opposite edge (globally) and connects
 *   the two midpoints to split the quad.
 *
 * Notes:
 * - Preserves probed maps (IDs 0..15) only when map polys are 1:1 with poly corners.
 * - Map midpoints are created per-poly (face-varying seams allowed). Geometry connectivity
 *   is what matters for edge/poly loops.
 */
class CmdConnect final : public Command
{
public:
    CmdConnect()           = default;
    ~CmdConnect() override = default;

    bool execute(class Scene* scene) override;
};
