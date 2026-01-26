#pragma once

#include <string_view>
#include <vector>

#include "Material.hpp"
#include "SysCounter.hpp"

class MaterialHandler
{
public:
    MaterialHandler();

    /**
     * @brief Create a new material or return existing index.
     *
     * If a material with the same name exists (case-insensitive) and
     * the name is not the default ("New Material"), returns its index.
     * Otherwise, adds a new material, appending a suffix like "_1", "_2", etc., if needed.
     *
     * @param name The desired material name (original capitalization preserved)
     * @return Index of the existing or newly created material
     */
    int32_t createMaterial(std::string_view name);

    void clear();

    [[nodiscard]] const std::vector<Material>& materials() const noexcept;

    [[nodiscard]] std::vector<Material>& materials() noexcept;

    [[nodiscard]] const Material& material(int index) const noexcept;

    [[nodiscard]] Material& material(int index) noexcept;

    SysCounterPtr changeCounter() const;

private:
    std::vector<Material> m_materials;
    SysCounterPtr         m_changeCounter;
};
