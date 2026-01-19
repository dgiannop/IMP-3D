// #include "MoveHandle.hpp"

// #include "CoreUtilities.hpp" // for un::ray, etc. (or wherever your ray type lives)

// // --------------------------------------------------------
// // Ctor
// // --------------------------------------------------------

// MoveHandle::MoveHandle(Type type, const glm::ivec3& axisMask, glm::vec3* position) :
//     m_type(type),
//     m_axisMask(axisMask),
//     m_position(position)
// {
// }

// // --------------------------------------------------------
// // Helpers
// // --------------------------------------------------------

// glm::vec3 MoveHandle::axisDirectionWorld(Viewport* /*vp*/) const
// {
//     // World axes for now; if you want gizmo aligned to object later,
//     // you can feed in a basis matrix instead.
//     glm::vec3 axis{0.0f};

//     if (m_axisMask.x != 0)
//         axis.x = static_cast<float>(m_axisMask.x);
//     if (m_axisMask.y != 0)
//         axis.y = static_cast<float>(m_axisMask.y);
//     if (m_axisMask.z != 0)
//         axis.z = static_cast<float>(m_axisMask.z);

//     if (glm::length2(axis) == 0.0f)
//         axis = glm::vec3{1.0f, 0.0f, 0.0f};

//     return glm::normalize(axis);
// }

// glm::vec3 MoveHandle::planeNormalWorld(Viewport* vp) const
// {
//     // Plane is defined by the *missing* axis of axisMask.
//     glm::vec3 n{0.0f};

//     // Example: mask (1,1,0) -> normal (0,0,1)
//     if (m_axisMask.x == 0 && (m_axisMask.y != 0 || m_axisMask.z != 0))
//         n.x = 1.0f;
//     else if (m_axisMask.y == 0 && (m_axisMask.x != 0 || m_axisMask.z != 0))
//         n.y = 1.0f;
//     else if (m_axisMask.z == 0 && (m_axisMask.x != 0 || m_axisMask.y != 0))
//         n.z = 1.0f;

//     // For CENTER handle we want a screen-space plane.
//     if (m_type == Type::CENTER || glm::length2(n) == 0.0f)
//     {
//         // Use camera forward as plane normal.
//         glm::mat4 iv = glm::inverse(vp->view()); // vp->invViewMatrix();
//         glm::vec3 f  = glm::normalize(glm::vec3{-iv[2][0], -iv[2][1], -iv[2][2]});
//         return f;
//     }

//     return glm::normalize(n);
// }

// // --------------------------------------------------------
// // Drag begin / drag
// // --------------------------------------------------------

// void MoveHandle::beginDrag(Viewport* vp, int mouseX, int mouseY)
// {
//     m_dragging   = true;
//     m_startPos   = *m_position;
//     m_startWorld = *m_position;

//     if (m_type == Type::AXIS)
//         m_axisWorld = axisDirectionWorld(vp);
//     else
//         m_axisWorld = glm::vec3{0.0f}; // unused for planes/center
// }

// void MoveHandle::drag(Viewport* vp, int mouseX, int mouseY)
// {
//     if (!m_dragging || !m_position)
//         return;

//     // Ray from camera through current mouse position.
//     un::ray r = vp->ray(mouseX, mouseY);

//     // ---------------------------------------------------------------------
//     // AXIS: project mouse movement onto axis in world space.
//     // ---------------------------------------------------------------------
//     if (m_type == Type::AXIS)
//     {
//         // Construct a plane that contains the axis and is roughly facing camera.
//         glm::vec3 camDir = vp->viewDirection();
//         glm::vec3 n      = glm::normalize(glm::cross(m_axisWorld, camDir));

//         // Fallback if camera is parallel to axis
//         if (glm::length2(n) < 1e-4f)
//             n = glm::normalize(glm::cross(m_axisWorld, glm::vec3{0.0f, 1.0f, 0.0f}));

//         // Intersect ray with this plane: dot((O + tD - P0), n) = 0
//         glm::vec3 O  = r.org;
//         glm::vec3 D  = r.dir;
//         glm::vec3 P0 = m_startWorld;

//         float denom = glm::dot(D, n);
//         if (std::abs(denom) < 1e-5f)
//             return;

//         float     t   = glm::dot(P0 - O, n) / denom;
//         glm::vec3 hit = O + D * t;

//         // Project onto axis
//         float d     = glm::dot(hit - m_startWorld, m_axisWorld);
//         *m_position = m_startPos + m_axisWorld * d;
//         return;
//     }

//     // ---------------------------------------------------------------------
//     // PLANE / CENTER: move in a plane.
//     // ---------------------------------------------------------------------
//     glm::vec3 n = planeNormalWorld(vp);

//     glm::vec3 O  = r.org;
//     glm::vec3 D  = r.dir;
//     glm::vec3 P0 = m_startWorld;

//     float denom = glm::dot(D, n);
//     if (std::abs(denom) < 1e-5f)
//         return;

//     float     t   = glm::dot(P0 - O, n) / denom;
//     glm::vec3 hit = O + D * t;

//     // For plane move, constrain to two axes.
//     glm::vec3 delta = hit - m_startWorld;

//     if (m_type == Type::PLANE)
//     {
//         if (m_axisMask.x == 0)
//             delta.x = 0.0f;
//         if (m_axisMask.y == 0)
//             delta.y = 0.0f;
//         if (m_axisMask.z == 0)
//             delta.z = 0.0f;
//     }

//     *m_position = m_startPos + delta;
// }

// // --------------------------------------------------------
// // construct()
// // --------------------------------------------------------

// void MoveHandle::construct(Viewport* /*vp*/, OverlayHandler& overlay) const
// {
//     // Simple geometry: one arrow (line + small box) for axis,
//     // small square for plane, and circle for center.

//     const float len   = 0.7f;
//     const float boxSz = 0.06f;

//     const glm::vec3 P = *m_position;

//     if (m_type == Type::AXIS)
//     {
//         glm::vec3 dir = axisDirectionWorld(nullptr); // world axis

//         glm::vec3 tip = P + dir * len;

//         overlay.add_line(P, tip);

//         // Small box at the tip
//         glm::vec3 bmin = tip - glm::vec3(boxSz * 0.5f);
//         glm::vec3 bmax = tip + glm::vec3(boxSz * 0.5f);
//         overlay.add_box(bmin, bmax);
//     }
//     else if (m_type == Type::PLANE)
//     {
//         glm::vec3 u{0.0f};
//         glm::vec3 v{0.0f};

//         if (m_axisMask.x != 0 && m_axisMask.y != 0)
//         {
//             u = glm::vec3(len * 0.3f, 0.0f, 0.0f);
//             v = glm::vec3(0.0f, len * 0.3f, 0.0f);
//         }
//         else if (m_axisMask.y != 0 && m_axisMask.z != 0)
//         {
//             u = glm::vec3(0.0f, len * 0.3f, 0.0f);
//             v = glm::vec3(0.0f, 0.0f, len * 0.3f);
//         }
//         else // XZ
//         {
//             u = glm::vec3(len * 0.3f, 0.0f, 0.0f);
//             v = glm::vec3(0.0f, 0.0f, len * 0.3f);
//         }

//         overlay.add_quad(P, P + u, P + u + v, P + v);
//     }
//     else // CENTER
//     {
//         overlay.add_circle(P, len * 0.3f);
//     }
// }
