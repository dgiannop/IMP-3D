// #pragma once

// #include <glm/glm.hpp>

// #include "OverlayHandler.hpp"
// #include "Viewport.hpp"

// // Simple move handle used by MoveGizmo.
// // Very similar in spirit to SizeHandle, but operates on a position only.
// class MoveHandle
// {
// public:
//     enum class Type
//     {
//         AXIS,  ///< Constrained along one axis (X, Y, or Z).
//         PLANE, ///< Constrained in a plane (XY, YZ, ZX).
//         CENTER ///< Free move in screen plane.
//     };

//     MoveHandle(Type type, const glm::ivec3& axisMask, glm::vec3* position);

//     /// Build the overlay geometry for this handle (arrow, square, or circle).
//     void construct(Viewport* vp, OverlayHandler& overlay) const;

//     /// Begin dragging from the given mouse position.
//     void beginDrag(Viewport* vp, int mouseX, int mouseY);

//     /// Update drag using the new mouse position.
//     void drag(Viewport* vp, int mouseX, int mouseY);

//     /// End dragging – currently just clears internal state.
//     void endDrag(Viewport* /*vp*/, int /*mouseX*/, int /*mouseY*/)
//     {
//         m_dragging = false;
//     }

//     const glm::ivec3& axisMask() const
//     {
//         return m_axisMask;
//     }

// private:
//     Type       m_type = Type::AXIS;
//     glm::ivec3 m_axisMask{}; // (1,0,0) = X, (0,1,0) = Y, (0,0,1) = Z, etc.
//     glm::vec3* m_position = nullptr;

//     bool      m_dragging = false;
//     glm::vec3 m_startPos{};
//     glm::vec3 m_startWorld{};
//     glm::vec3 m_axisWorld{}; // world axis direction used for constraint

//     // Helpers
//     glm::vec3 axisDirectionWorld(Viewport* vp) const;
//     glm::vec3 planeNormalWorld(Viewport* vp) const;
// };
