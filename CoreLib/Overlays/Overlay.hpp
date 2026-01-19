// #pragma once

// #include <glm/glm.hpp>
// #include <string>

// /**
//  * @brief Defines an overlay “handle” that is pickable.
//  *
//  * An Overlay groups one or more primitives (points, lines, polygons) and has:
//  *  - a logical name (e.g. "0", "handle_pos_x", "camera_main")
//  *  - an axis vector used for collinearity logic when picking
//  *
//  * The actual geometry lives in OverlayHandler; Overlay only carries metadata
//  * and the index ranges into the primitive arrays.
//  */
// class Overlay
// {
// public:
//     struct Range
//     {
//         std::size_t firstPoint   = 0;
//         std::size_t pointCount   = 0;
//         std::size_t firstLine    = 0;
//         std::size_t lineCount    = 0;
//         std::size_t firstPolygon = 0;
//         std::size_t polygonCount = 0;
//     };

//     explicit Overlay(const std::string& name);

//     /// @return The overlay name.
//     const std::string& name() const noexcept;

//     /// Set the overlay axis (used for collinearity picking heuristics).
//     void set_axis(const glm::vec3& axis) noexcept;

//     /// @return The overlay axis.
//     const glm::vec3& axis() const noexcept;

//     /// @return Mutable access to the primitive index ranges.
//     Range& range() noexcept;

//     /// @return Const access to the primitive index ranges.
//     const Range& range() const noexcept;

// private:
//     std::string m_name;
//     glm::vec3   m_axis{};
//     Range       m_range{};
// };
