// CmdDissolveEdge.hpp
#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Dissolve selected edges.
 *
 * Supports:
 *  - Quad edge-loop dissolve (removes an edge loop and rebuilds a quad strip)
 *  - Fallback: dissolve individual manifold edges (merges 2 adjacent polys into an n-gon)
 */
class CmdDissolveEdge final : public Command
{
public:
    bool execute(Scene* scene) override;
};
