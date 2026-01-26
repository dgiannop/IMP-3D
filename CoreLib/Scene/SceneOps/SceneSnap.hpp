#pragma once

#include <glm/glm.hpp>

/**
 * @brief Grid-based snapping helper for scene interactions.
 *
 * SceneSnap provides a lightweight, stateless snapping utility used
 * by tools and manipulators to align points to a regular grid.
 *
 * Snapping is applied in world space, with an optional origin offset.
 * If disabled, the input position is returned unchanged.
 */
class SceneSnap
{
public:
    /** @brief Construct a SceneSnap with default settings. */
    SceneSnap() = default;

    // ------------------------------------------------------------
    // Settings
    // ------------------------------------------------------------

    /**
     * @brief Enable or disable snapping.
     * @param v True to enable snapping
     */
    void setEnabled(bool v) noexcept
    {
        m_enabled = v;
    }

    /**
     * @brief Query whether snapping is enabled.
     * @return True if snapping is active
     */
    [[nodiscard]] bool enabled() const noexcept
    {
        return m_enabled;
    }

    /**
     * @brief Set the grid size used for snapping.
     * @param s Grid spacing (must be > 0 to take effect)
     */
    void setGridSize(float s) noexcept
    {
        m_grid = s;
    }

    /**
     * @brief Get the current grid size.
     * @return Grid spacing
     */
    [[nodiscard]] float gridSize() const noexcept
    {
        return m_grid;
    }

    /**
     * @brief Set the snapping origin.
     *
     * The origin is subtracted before snapping and re-applied after,
     * allowing grids offset from world origin.
     *
     * @param org Grid origin in world space
     */
    void setOrigin(const glm::vec3& org) noexcept
    {
        m_origin = org;
    }

    /**
     * @brief Get the snapping origin.
     * @return Grid origin in world space
     */
    [[nodiscard]] const glm::vec3& origin() const noexcept
    {
        return m_origin;
    }

    // ------------------------------------------------------------
    // Snapping
    // ------------------------------------------------------------

    /**
     * @brief Apply grid snapping to a position.
     *
     * If snapping is disabled or grid size is invalid (<= 0),
     * the input position is returned unchanged.
     *
     * @param p Input position in world space
     * @return Snapped position
     */
    [[nodiscard]] glm::vec3 apply(const glm::vec3& p) const noexcept
    {
        if (!m_enabled || m_grid <= 0.0f)
            return p;

        glm::vec3 local   = p - m_origin;
        glm::vec3 s       = local / m_grid;
        glm::vec3 snapped = glm::round(s) * m_grid;
        return snapped + m_origin;
    }

private:
    /** @brief Whether snapping is enabled. */
    bool m_enabled = false;

    /** @brief Grid spacing in world units. */
    float m_grid = 0.1f;

    /** @brief Grid origin in world space. */
    glm::vec3 m_origin = glm::vec3(0.0f);
};
