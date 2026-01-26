#include "MaterialHandler.hpp"

#include "CoreUtilities.hpp"

MaterialHandler::MaterialHandler() : m_changeCounter{std::make_shared<SysCounter>()}
{
}

int32_t MaterialHandler::createMaterial(std::string_view name)
{
    static constexpr const char* kDefaultName = "New Material";

    // Base name (preserve original capitalization if provided)
    const std::string base         = name.empty() ? kDefaultName : std::string{name};
    const std::string baseLower    = un::to_lower(base);
    const std::string defaultLower = un::to_lower(std::string{kDefaultName});

    // -----------------------------------------------------------------
    // 1) If NOT the default name:
    //    If a material with the same name exists (case-insensitive),
    //    return its index.
    // -----------------------------------------------------------------
    if (baseLower != defaultLower)
    {
        if (auto it = std::ranges::find_if(m_materials, [&](const Material& m) {
                return un::to_lower(m.name()) == baseLower;
            });
            it != m_materials.end())
        {
            return static_cast<int32_t>(std::distance(m_materials.begin(), it));
        }
    }
    // If we reach here:
    //  - either name == "New Material" (case-insensitive), or
    //  - name is custom but no existing material uses it.

    // -----------------------------------------------------------------
    // 2) Find a unique name (for both default + non-default cases)
    //    Append _1, _2, ... until we find a name that doesn't exist
    //    case-insensitively.
    // -----------------------------------------------------------------
    std::string unique = base;
    for (int suffix = 1;; ++suffix)
    {
        const std::string uniqLower = un::to_lower(unique);

        const bool exists = std::ranges::any_of(
            m_materials,
            [&](const Material& m) {
                return un::to_lower(m.name()) == uniqLower;
            });

        if (!exists)
        {
            Material mat(unique);
            mat.changeCounter()->addParent(m_changeCounter);
            // mat.setBaseColor({0.55f, 0.55f, 0.75f, 1.f}); // your default

            m_materials.push_back(std::move(mat));
            m_changeCounter->change();

            return static_cast<int32_t>(m_materials.size() - 1);
        }

        unique = base + "_" + std::to_string(suffix);
    }
}

void MaterialHandler::clear()
{
    m_materials.clear();
    m_changeCounter->change();
}

const std::vector<Material>& MaterialHandler::materials() const noexcept
{
    return m_materials;
}

std::vector<Material>& MaterialHandler::materials() noexcept
{
    return m_materials;
}

const Material& MaterialHandler::material(int index) const noexcept
{
    return m_materials[index];
}

Material& MaterialHandler::material(int index) noexcept
{
    return m_materials[index];
}

SysCounterPtr MaterialHandler::changeCounter() const
{
    return m_changeCounter;
}
