#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

/**
 * @defgroup Factories Factory System
 * @brief Generic runtime factory for Tools, Commands, and other pluggable components.
 *
 * The factory stores string → constructor mappings and allows components
 * (such as tools and commands) to be registered and created by name at runtime.
 */

/**
 * @class ItemFactory
 * @brief Generic factory for constructing items by string key.
 *
 * @ingroup Factories
 *
 * ItemFactory provides a lightweight registry mapping string identifiers
 * to constructor functions. It is used throughout the application to
 * dynamically instantiate:
 *
 *  - Tools (`ItemFactory<Tool>`)
 *  - Commands (`ItemFactory<Command>`)
 *  - Other pluggable systems
 *
 * Usage example:
 * @code
 * ItemFactory<Tool> toolFactory;
 * toolFactory.registerItem("Move",  &ItemFactory<Tool>::CreateItemType<MoveTool>);
 * auto tool = toolFactory.createItem("Move");
 * @endcode
 *
 * @tparam T Base type of items created by the factory.
 */
template<typename T>
class ItemFactory
{
public:
    ItemFactory() = default;

    /// Function pointer / functor used to create new items.
    using CreateFunc = std::function<std::unique_ptr<T>()>;

    /**
     * @brief Register a new item type under a name.
     *
     * Registers a factory function that creates an instance of a derived class.
     * If the name already exists, the previous entry is replaced.
     *
     * @param name       Unique string identifier for the item type.
     * @param createFunc Function that constructs a new instance.
     */
    void registerItem(const std::string& name, CreateFunc createFunc)
    {
        registry[name] = std::move(createFunc);
    }

    /**
     * @brief Create an item instance by name.
     *
     * @param name Registered string key.
     * @return A newly constructed unique_ptr<T>, or nullptr if not found.
     */
    std::unique_ptr<T> createItem(const std::string& name) const
    {
        if (auto it = registry.find(name); it != registry.end())
        {
            return it->second();
        }
        return nullptr;
    }

    /**
     * @brief Helper function that constructs items of a specific derived type.
     *
     * Useful for registration:
     * @code
     * factory.registerItem("Sphere", &ItemFactory<Tool>::CreateItemType<SphereTool>);
     * @endcode
     *
     * @tparam Derived The concrete type to construct (must derive from T).
     * @return std::unique_ptr<Derived> Newly allocated item.
     */
    template<typename Derived>
    static std::unique_ptr<Derived> createItemType()
    {
        return std::make_unique<Derived>();
    }

private:
    /// Map of registered item names to constructor functions.
    std::unordered_map<std::string, CreateFunc> registry;
};
