#pragma once

#include "ItemFactory.hpp"
#include "SceneFormat.hpp"

class Tool;
class Command;
class SceneFormat;

namespace config
{

    /**
     * @brief Register all available Tool types into the given tool factory.
     */
    void registerTools(ItemFactory<Tool>& factory);

    /**
     * @brief Register all available Command types into the given command factory.
     */
    void registerCommands(ItemFactory<Command>& factory);

    /**
     * @brief Register all supported scene file formats (import/export handlers).
     *
     * This should be called at startup to ensure that file I/O formats (OBJ,
     * native IMP3D, glTF, etc.) are recognized by the application.
     *
     * Typical usage:
     * @code
     * ItemFactory<SceneFormat> formatFactory;
     * registerSceneFormats(formatFactory);
     * @endcode
     *
     * @param factory Factory instance that will receive registered formats.
     */
    void registerSceneFormats(ItemFactory<SceneFormat>& factory);

} // namespace config
