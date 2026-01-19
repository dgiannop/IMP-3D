#pragma once

#include "Command.hpp"

class Scene;

/**
 * @class CmdSelectAll
 * @brief Selects all elements of the active selection mode.
 *
 * Depending on Scene::selectionMode(), selects:
 *  - all vertices
 *  - all edges
 *  - all polygons
 *
 * Clears selection in the other modes to keep selection state consistent.
 */
class CmdSelectAll : public Command
{
public:
    /// Execute the select-all command.
    bool execute(Scene* scene) override;
};

/**
 * @class CmdSelectNone
 * @brief Clears all element selections.
 *
 * Clears vertex, edge, and polygon selection on all visible SceneMeshes.
 */
class CmdSelectNone : public Command
{
public:
    /// Execute the clear-selection command.
    bool execute(Scene* scene) override;
};

/**
 * @class CmdEdgeLoop
 * @brief Replaces the current edge selection with an edge loop.
 *
 * Uses SysMesh::edge_loop() to compute a canonical edge loop
 * starting from the first selected edge.
 */
class CmdEdgeLoop : public Command
{
public:
    /// Execute the edge loop selection command.
    bool execute(Scene* scene) override;
};

/**
 * @class CmdEdgeRing
 * @brief Replaces the current edge selection with an edge ring.
 *
 * Uses SysMesh::edge_ring() to compute a canonical edge ring
 * starting from the first selected edge.
 */
class CmdEdgeRing : public Command
{
public:
    /// Execute the edge ring selection command.
    bool execute(Scene* scene) override;
};
