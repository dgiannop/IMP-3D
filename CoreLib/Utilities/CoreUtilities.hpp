#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <concepts>
#include <functional> // for std::hash
#include <glm/ext/scalar_constants.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtx/norm.hpp>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility> // for std::pair

/**
 * @defgroup MathUtils Math / Geometry Utilities
 * @brief Small utilities for robust float comparisons, snapping, rounding, rays, and intersections.
 *
 * Helpers here are lightweight wrappers around GLM or simple algorithms used
 * across selection, picking, transforms, and editor tooling.
 */
namespace un
{

    /**
     * @brief Simple ray type used for picking and intersections.
     * @ingroup MathUtils
     *      * @note `dir` should be normalized. `inv` is the component-wise inverse of `dir`
     * (i.e., `1.0f / dir`) and is cached for faster AABB tests.
     */
    struct ray
    {
        glm::vec3 org; ///< Origin of the ray in 3D space.
        glm::vec3 dir; ///< Direction vector (should be normalized).
        glm::vec3 inv; ///< 1.0f / dir (component-wise); used for fast AABB tests.
    };

    /**
     * @brief Generic floating-point equality check using GLM epsilon.
     * @tparam T A floating-point type.
     * @param a First value.
     * @param b Second value.
     * @return True if |a - b| <= epsilon(T).
     * @ingroup MathUtils
     */
    template<std::floating_point T>
    constexpr bool equal(T a, T b)
    {
        return glm::epsilonEqual(a, b, glm::epsilon<T>());
    }

    /**
     * @brief vec3 equality using squared length and epsilon.
     * @param a First vector.
     * @param b Second vector.
     * @return True if length²(a - b) <= epsilon(float).
     * @ingroup MathUtils
     */
    inline bool equal(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length2(a - b) <= glm::epsilon<float>();
    }

    /**
     * @brief Generic floating-point zero check.
     * @tparam T Floating-point type.
     * @param val Value to test.
     * @return True if |val| <= epsilon(T).
     * @ingroup MathUtils
     */
    template<std::floating_point T>
    constexpr bool is_zero(T val)
    {
        return glm::abs(val) <= glm::epsilon<T>();
    }

    /**
     * @brief Zero check for vec3 using squared length and epsilon.
     * @param v Vector to test.
     * @return True if length²(v) <= 10 * epsilon(float).
     * @ingroup MathUtils
     */
    inline bool is_zero(const glm::vec3& v)
    {
        return glm::length2(v) <= (10 * glm::epsilon<float>());
    }

    /**
     * @brief Zero check for ivec3 (exact equality).
     * @param v Integer vector to test.
     * @return True if all components are zero.
     * @ingroup MathUtils
     */
    constexpr bool is_zero(const glm::ivec3& v)
    {
        return v.x == 0 && v.y == 0 && v.z == 0;
    }

    /**
     * @brief Convert a string to lowercase (ASCII).
     *
     * Uses `std::ranges::transform` and `std::tolower`. Useful for
     * case-insensitive name comparisons in the editor.
     *
     * @param str Input string (copied by value).
     * @return Lowercase version of @p str.
     * @ingroup MathUtils
     */
    inline std::string to_lower(std::string str)
    {
        std::ranges::transform(str, str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return str;
    }

    /**
     * @brief Normalize a vector safely (avoids NaNs for tiny/invalid inputs).
     *
     * Behaves like `glm::normalize()` but returns (0,0,0) if the vector length
     * is near zero or non-finite.
     *
     * @param v   Input vector.
     * @param eps Threshold under which the vector is treated as zero.
     * @return Normalized vector, or zero on degenerate input.
     * @ingroup MathUtils
     */
    inline glm::vec3 safe_normalize(const glm::vec3& v, float eps = 1e-8f)
    {
        float len2 = glm::dot(v, v);
        if (len2 > eps * eps && std::isfinite(len2))
            return v / std::sqrt(len2);
        return glm::vec3(0.0f);
    }

    /**
     * @brief Normalize a vector safely with a fallback.
     *
     * Behaves like `glm::normalize()` but avoids NaNs for tiny or invalid inputs.
     * If the vector length is near zero or non-finite, the provided fallback
     * vector is returned instead.
     *
     * @param v        Input vector.
     * @param fallback Vector to return if @p v is degenerate.
     * @param eps      Threshold under which the vector is treated as zero.
     * @return Normalized vector, or @p fallback on degenerate input.
     * @ingroup MathUtils
     */
    inline glm::vec3 safe_normalize(const glm::vec3& v,
                                    const glm::vec3& fallback,
                                    float            eps = 1e-8f)
    {
        float len2 = glm::dot(v, v);
        if (len2 > eps * eps && std::isfinite(len2))
            return v / std::sqrt(len2);
        return fallback;
    }

    /**
     * @brief Find the closest point on a segment to a point (parametric form).
     *
     * @param pt Query point.
     * @param a  Segment start.
     * @param b  Segment end.
     * @return t in [0,1] such that a + t*(b-a) is the closest point to @p pt.
     * @ingroup MathUtils
     */
    inline float closest_point_on_line(const glm::vec2& pt,
                                       const glm::vec2& a,
                                       const glm::vec2& b)
    {
        glm::vec2 ab       = b - a;
        float     lengthSq = glm::length2(ab); // Squared length (avoids sqrt)

        if (lengthSq == 0.0f)
            return 0.0f; // Edge case: a == b

        return glm::dot(pt - a, ab) / lengthSq;
    }

    /**
     * @brief Snap a 3D position to a uniform grid spacing.
     * @param position World position.
     * @param gridSize Grid cell size (default 0.1).
     * @return Snapped position.
     * @ingroup MathUtils
     */
    constexpr glm::vec3 snapToGrid(const glm::vec3& position, float gridSize = 0.1f)
    {
        return glm::vec3(
            std::round(position.x / gridSize) * gridSize,
            std::round(position.y / gridSize) * gridSize,
            std::round(position.z / gridSize) * gridSize);
    }

    /**
     * @brief Principal axis enumeration.
     * @ingroup MathUtils
     */
    enum class axis
    {
        X = 0, ///< X axis.
        Y = 1, ///< Y axis.
        Z = 2  ///< Z axis.
    };

    /**
     * @brief Returns the dominant axis of a 3D vector (by absolute component).
     * @tparam T Vector type with x/y/z members (e.g., glm::vec3).
     * @param val Input vector.
     * @return The enum value of the dominant axis.
     * @ingroup MathUtils
     */
    template<class T>
    constexpr axis to_axis(T val)
    {
        val = glm::abs(val);
        if (val.x > val.y && val.x > val.z)
            return axis::X;
        if (val.y > val.x && val.y > val.z)
            return axis::Y;
        return axis::Z;
    }

    /**
     * @brief Intersect a ray with a triangle (shear-projection method).
     *
     * @param r     Input ray (org/dir/inv).
     * @param a     Triangle vertex A.
     * @param b     Triangle vertex B.
     * @param c     Triangle vertex C.
     * @param out_t Distance along the ray to the hit point (if any).
     * @return True if the ray intersects the triangle.
     * @ingroup MathUtils
     */
    bool ray_triangle_intersect(const ray&       r,
                                const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c,
                                float&           out_t);

    /**
     * @brief Intersect two 3D line segments.
     *
     * @param a1 First segment start.
     * @param a2 First segment end.
     * @param b1 Second segment start.
     * @param b2 Second segment end.
     * @param out_t1 Param on segment A in [0,1] at intersection (if any).
     * @param out_t2 Param on segment B in [0,1] at intersection (if any).
     * @return True if the segments intersect (or overlap per implementation).
     * @ingroup MathUtils
     */
    bool line_intersect(const glm::vec3& a1, const glm::vec3& a2, const glm::vec3& b1, const glm::vec3& b2, float& out_t1, float& out_t2);

    /**
     * @brief Round a float/double to a fixed number of decimal digits.
     * @tparam T float or double
     * @param value Value to round.
     * @param digits Number of decimal digits (default 5).
     * @return Rounded value.
     * @ingroup MathUtils
     */
    template<std::floating_point T>
    constexpr T roundToPrecision(T value, int digits = 5)
    {
        T factor = std::pow(T{10}, static_cast<T>(digits));
        return std::round(value * factor) / factor;
    }

    /**
     * @brief Component-wise rounding for vec3.
     * @param v Vector to round.
     * @param digits Decimal digits (default 5).
     * @return Rounded vector.
     * @ingroup MathUtils
     */
    constexpr glm::vec3 roundToPrecision(const glm::vec3& v, int digits = 5)
    {
        return glm::vec3(
            roundToPrecision(v.x, digits),
            roundToPrecision(v.y, digits),
            roundToPrecision(v.z, digits));
    }

    /**
     * @brief Component-wise rounding for any GLM vector (vec1..vec4).
     * @tparam L Length of the vector.
     * @tparam T Scalar type (floating-point).
     * @tparam Q Qualifier.
     * @param v GLM vector to round.
     * @param digits Decimal digits (default 5).
     * @return Rounded vector.
     * @ingroup MathUtils
     */
    template<glm::length_t L, typename T, glm::qualifier Q>
        requires std::floating_point<T>
    constexpr glm::vec<L, T, Q> roundToPrecision(const glm::vec<L, T, Q>& v, int digits = 5)
    {
        glm::vec<L, T, Q> out{};
        for (glm::length_t i = 0; i < L; ++i)
            out[i] = roundToPrecision(v[i], digits);
        return out;
    }

    /**
     * @brief Component-wise rounding for any GLM matrix.
     * @tparam C Columns
     * @tparam R Rows
     * @tparam T Scalar type
     * @tparam Q Qualifier
     * @param m Matrix to round.
     * @param digits Decimal digits (default 5).
     * @return Rounded matrix.
     * @ingroup MathUtils
     */
    template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
    constexpr glm::mat<C, R, T, Q> roundToPrecision(const glm::mat<C, R, T, Q>& m, int digits = 5)
    {
        glm::mat<C, R, T, Q> result(0);
        for (glm::length_t c = 0; c < C; ++c)
            for (glm::length_t r = 0; r < R; ++r)
                result[c][r] = roundToPrecision(m[c][r], digits);
        return result;
    }

    /**
     * @brief Pack an undirected pair of 32-bit ints into a 64-bit key.
     *
     * Ensures (a,b) and (b,a) produce the same key by sorting the pair first.
     * Commonly used for edge keys in hash sets/maps.
     *
     * @param a First integer.
     * @param b Second integer.
     * @return 64-bit packed key.
     * @ingroup MathUtils
     */
    constexpr uint64_t pack_undirected_i32(int32_t a, int32_t b) noexcept
    {
        if (a > b)
            std::swap(a, b);

        return (uint64_t(uint32_t(a)) << 32) | uint64_t(uint32_t(b));
    }

    /**
     * @brief Construct an orthonormal tangent basis (U,V) from a normal N.
     *
     * Picks a stable "up" vector to avoid degeneracy when N is near the world Z axis.
     * U and V are normalized. If N is near zero, U and V become zero vectors.
     *
     * @param N Input normal (does not have to be normalized).
     * @param out_U Output tangent U.
     * @param out_V Output bitangent V.
     * @ingroup MathUtils
     */
    inline void make_basis(const glm::vec3& N, glm::vec3& out_U, glm::vec3& out_V) noexcept
    {
        const glm::vec3 n = un::safe_normalize(N);
        if (glm::dot(n, n) < 1e-12f)
        {
            out_U = glm::vec3(0.0f);
            out_V = glm::vec3(0.0f);
            return;
        }

        const glm::vec3 up = (std::abs(n.z) < 0.999f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        out_U              = un::safe_normalize(glm::cross(up, n));
        out_V              = un::safe_normalize(glm::cross(n, out_U));
    }

    /**
     * @brief Intersect two infinite 2D lines in parametric form.
     *
     * Lines:
     *  L0(t) = p0 + t*d0
     *  L1(u) = p1 + u*d1
     *
     * @param p0 Point on line 0.
     * @param d0 Direction of line 0 (does not have to be normalized).
     * @param p1 Point on line 1.
     * @param d1 Direction of line 1 (does not have to be normalized).
     * @param out Intersection point (if any).
     * @return True if the lines are not parallel and intersection exists.
     * @ingroup MathUtils
     */
    inline bool intersect_lines_2d(const glm::vec2& p0,
                                   const glm::vec2& d0,
                                   const glm::vec2& p1,
                                   const glm::vec2& d1,
                                   glm::vec2&       out) noexcept
    {
        const float det = d0.x * d1.y - d0.y * d1.x;
        if (std::abs(det) < 1e-10f)
            return false;

        const glm::vec2 r = p1 - p0;
        const float     t = (r.x * d1.y - r.y * d1.x) / det;
        out               = p0 + t * d0;
        return true;
    }

    /**
     * @brief Create a runtime_error enriched with source location info.
     *
     * Example output:
     *   Tool "BoxTool" not found [at Core::setActiveTool]
     */
    inline std::runtime_error core_exception(
        const std::string&   msg,
        std::source_location loc = std::source_location::current())
    {
        // --- Shorten file name ---
        std::string file = loc.file_name();
        if (auto pos = file.find_last_of("/\\"); pos != std::string::npos)
            file = file.substr(pos + 1);

        // --- Simplify the function signature ---
        std::string func = loc.function_name();

        // Remove MSVC calling conventions (e.g., "__cdecl")
        const char* calling_convs[] = {"__cdecl", "__stdcall", "__fastcall"};
        for (auto cc : calling_convs)
        {
            if (auto pos = func.find(cc); pos != std::string::npos)
            {
                func.erase(pos, std::string(cc).size());
            }
        }

        // Replace parameter list with "..."
        if (auto open = func.find('('); open != std::string::npos)
        {
            if (auto close = func.rfind(')'); close != std::string::npos && close > open)
            {
                func.replace(open + 1, close - open - 1, "...");
            }
        }

        // Trim leftover double spaces if any
        while (func.find("  ") != std::string::npos)
            func.erase(func.find("  "), 1);

        // --- Build final error message ---
        return std::runtime_error(
            msg + " [at " + file + ":" + std::to_string(loc.line()) +
            " in " + func + "]");
    }

} // namespace un

/**
 * @brief Hash specialization for std::pair<int,int>.
 * @ingroup MathUtils
 *
 * Used for edge keys, grid bins, or any small integer pair indexing.
 */
namespace std
{
    template<>
    struct hash<std::pair<int, int>>
    {
        std::size_t operator()(const std::pair<int, int>& p) const noexcept
        {
            return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
        }
    };

} // namespace std

#define TICK(NAME) \
    const auto __tick_##NAME = std::chrono::high_resolution_clock::now();

#define TOCK(NAME)                                                            \
    do                                                                        \
    {                                                                         \
        const auto __tock_##NAME = std::chrono::high_resolution_clock::now(); \
        const auto __dt_##NAME =                                              \
            std::chrono::duration_cast<std::chrono::microseconds>(            \
                __tock_##NAME - __tick_##NAME)                                \
                .count();                                                     \
        std::cerr << #NAME << " took: " << __dt_##NAME / 1000.0 << " ms\n";   \
    }                                                                         \
    while (0)
