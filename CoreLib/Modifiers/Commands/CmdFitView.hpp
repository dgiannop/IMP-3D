#pragma once

#include "Command.hpp"

class Scene;

/**
 * @brief Fit the active viewport to the current selection (if any), otherwise to all visible meshes.
 */
class CmdFitView final : public Command
{
public:
    bool execute(Scene* scene) override;
};
