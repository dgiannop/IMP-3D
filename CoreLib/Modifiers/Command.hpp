#pragma once

/**
 * @class Command
 * @brief Base class for all executable scene commands (undoable actions, operations, etc.).
 *
 * Commands represent actions that modify the Scene. Each command implements its own
 * execution logic, allowing the command system to encapsulate operations such as
 * transformations, mesh edits, creation/deletion, etc.
 */
class Command
{
public:
    virtual ~Command() = default;

    /**
     * @brief Execute the command.
     *
     * @param scene The scene on which the command will operate.
     * @return True if the command executed successfully, false otherwise.
     */
    virtual bool execute(class Scene* scene) = 0;
};
