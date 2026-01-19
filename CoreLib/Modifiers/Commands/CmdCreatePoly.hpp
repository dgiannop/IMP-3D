#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Create a polygon from selected vertices (per mesh).
 *
 * Useful for filling gaps / holes by selecting boundary verts and running the command.
 *
 * Behavior:
 *  - For each mesh with selected verts:
 *      - Deduplicate verts, require >= 3
 *      - Compute centroid
 *      - Estimate a best-effort plane normal
 *      - Project to 2D and sort radially (angle sort)
 *      - Create poly using SysMesh::create_poly(sortedVerts)
 *      - Reselect the new poly (optional UX) and clear the input vert selection
 *
 * Notes:
 *  - This is a best-effort ordering method. If the selected verts are highly non-planar
 *    or self-intersecting in projection, results may be undesirable.
 */
class CmdCreatePoly final : public Command
{
public:
    bool execute(Scene* scene) override;
};
