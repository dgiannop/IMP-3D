#pragma once

#include "Command.hpp"

class Scene;

class CmdDelete : public Command
{
public:
    bool execute(Scene* scene) override;
};
