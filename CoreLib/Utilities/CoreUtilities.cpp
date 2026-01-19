#include "CoreUtilities.hpp"

namespace un
{
    bool line_intersect(const glm::vec3& a1, const glm::vec3& a2, const glm::vec3& b1, const glm::vec3& b2, float& out_t1, float& out_t2)
    {
        // Robust segment–segment closest approach
        // Works well for coplanar inset / bevel geometry

        constexpr float EPS = 1e-12f;

        const glm::vec3 u = a2 - a1;
        const glm::vec3 v = b2 - b1;
        const glm::vec3 w = a1 - b1;

        const float A = glm::dot(u, u);
        const float B = glm::dot(u, v);
        const float C = glm::dot(v, v);
        const float D = glm::dot(u, w);
        const float E = glm::dot(v, w);

        const float denom = A * C - B * B;

        // Parallel or degenerate
        if (std::abs(denom) < EPS)
            return false;

        float s = (B * E - C * D) / denom;
        float t = (A * E - B * D) / denom;

        // Clamp to segment ranges
        s = std::clamp(s, 0.0f, 1.0f);
        t = std::clamp(t, 0.0f, 1.0f);

        const glm::vec3 pA = a1 + s * u;
        const glm::vec3 pB = b1 + t * v;

        // Must actually meet (coplanar intersection)
        if (glm::length2(pA - pB) > 1e-8f)
            return false;

        out_t1 = s;
        out_t2 = t;
        return true;
    }

} // namespace un
