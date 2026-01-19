#include "MaterialEditor.hpp"

#include <algorithm>
#include <cstdint>

#include "CoreUtilities.hpp"
#include "Material.hpp"
#include "MaterialHandler.hpp"
#include "Scene.hpp"

MaterialEditor::MaterialEditor(Scene* scene) noexcept :
    m_scene(scene)
{
}

void MaterialEditor::setScene(Scene* scene) noexcept
{
    m_scene = scene;
}

Scene* MaterialEditor::scene() const noexcept
{
    return m_scene;
}

const MaterialHandler* MaterialEditor::handler() const noexcept
{
    if (!m_scene)
        return nullptr;

    return m_scene->materialHandler();
}

MaterialHandler* MaterialEditor::handler() noexcept
{
    if (!m_scene)
        return nullptr;

    // Preferred: provide a non-const overload on Scene:
    //   MaterialHandler* materialHandler() noexcept;
    //
    // If you haven't added it yet, add it (recommended), then this compiles cleanly:
    return m_scene->materialHandler();
}

std::vector<MaterialEditor::Entry> MaterialEditor::list() const
{
    std::vector<Entry> out = {};

    const MaterialHandler* mh = handler();
    if (!mh)
        return out;

    const auto& mats = mh->materials();
    out.reserve(mats.size());

    for (int32_t i = 0; i < static_cast<int32_t>(mats.size()); ++i)
    {
        const Material& m = mats[i];

        Entry e = {};
        e.id    = i;
        e.name  = m.name();

        out.push_back(std::move(e));
    }

    return out;
}

const Material* MaterialEditor::material(int32_t id) const noexcept
{
    const MaterialHandler* mh = handler();
    if (!mh)
        return nullptr;

    const auto& mats = mh->materials();
    if (id < 0 || id >= static_cast<int32_t>(mats.size()))
        return nullptr;

    return &mats[static_cast<size_t>(id)];
}

Material* MaterialEditor::material(int32_t id) noexcept
{
    MaterialHandler* mh = handler();
    if (!mh)
        return nullptr;

    auto& mats = mh->materials();
    if (id < 0 || id >= static_cast<int32_t>(mats.size()))
        return nullptr;

    return &mats[static_cast<size_t>(id)];
}

int32_t MaterialEditor::findByName(std::string_view name) const
{
    const MaterialHandler* mh = handler();
    if (!mh)
        return -1;

    const auto& mats = mh->materials();

    // Case-insensitive compare using your existing helper.
    // MaterialHandler uses un::to_lower() internally; we reuse it here.
    const std::string keyLower = un::to_lower(std::string{name});

    for (int32_t i = 0; i < static_cast<int32_t>(mats.size()); ++i)
    {
        const Material& m = mats[static_cast<size_t>(i)];
        if (un::to_lower(m.name()) == keyLower)
            return i;
    }

    return -1;
}

int32_t MaterialEditor::createOrGet(std::string_view name)
{
    MaterialHandler* mh = handler();
    if (!mh)
        return -1;

    return mh->createMaterial(name);
}

SysCounterPtr MaterialEditor::changeCounter() const
{
    const MaterialHandler* mh = handler();
    if (!mh)
        return {};

    return mh->changeCounter();
}
