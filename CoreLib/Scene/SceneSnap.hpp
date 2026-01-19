#pragma once

#include <glm/glm.hpp>

class SceneSnap
{
public:
    SceneSnap() = default;

    // Settings
    void setEnabled(bool v) noexcept
    {
        m_enabled = v;
    }

    bool enabled() const noexcept
    {
        return m_enabled;
    }

    void setGridSize(float s) noexcept
    {
        m_grid = s;
    }

    float gridSize() const noexcept
    {
        return m_grid;
    }

    void setOrigin(const glm::vec3& org) noexcept
    {
        m_origin = org;
    }

    const glm::vec3& origin() const noexcept
    {
        return m_origin;
    }

    // Main snap function
    glm::vec3 apply(const glm::vec3& p) const noexcept
    {
        if (!m_enabled || m_grid <= 0.0f)
            return p;

        glm::vec3 local   = p - m_origin;
        glm::vec3 s       = local / m_grid;
        glm::vec3 snapped = glm::round(s) * m_grid;
        return snapped + m_origin;
    }

private:
    bool      m_enabled = false;
    float     m_grid    = 0.1f;
    glm::vec3 m_origin  = glm::vec3(0.0f); // grid origin (optional future use)
};
