#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Expands the current selection to all connected elements,
 *        using the current SelectionMode (verts/edges/polys).
 *
 * - VERTS: connected via edges (vertex adjacency)
 * - EDGES: connected via shared vertices
 * - POLYS: connected via shared edges (face islands)
 */
class CmdSelectConnected final : public Command
{
public:
    bool execute(Scene* scene) override;
};
