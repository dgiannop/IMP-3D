#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief World-axis XYZ rotate gizmo (ring handles).
 *
 * Overlays:
 *  - handle 0: X ring (rotate around +X)
 *  - handle 1: Y ring (rotate around +Y)
 *  - handle 2: Z ring (rotate around +Z)
 *
 * Tool owns m_amountDeg; gizmo writes absolute delta (degrees).
 */
class RotateGizmo final
{
public:
    explicit RotateGizmo(glm::vec3* amountDeg);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler& overlayHandler() noexcept
    {
        return m_overlayHandler;
    }

    const OverlayHandler& overlayHandler() const noexcept
    {
        return m_overlayHandler;
    }

private:
    enum class Axis
    {
        None = -1,
        X    = 0,
        Y    = 1,
        Z    = 2,
    };

    static Axis axisFromHandle(int32_t h) noexcept
    {
        switch (h)
        {
            case 0:
                return Axis::X;
            case 1:
                return Axis::Y;
            case 2:
                return Axis::Z;
            default:
                return Axis::None;
        }
    }

    static glm::vec3 axisDir(Axis a) noexcept;

    static void buildOrthonormalBasis(const glm::vec3& n, glm::vec3& outU, glm::vec3& outV) noexcept;

    [[nodiscard]] bool ringPlaneHit(Viewport*        vp,
                                    const glm::vec3& origin,
                                    const glm::vec3& axisN,
                                    float            mx,
                                    float            my,
                                    glm::vec3&       outHit) const noexcept;

    [[nodiscard]] float signedAngleOnPlane(const glm::vec3& axisN,
                                           const glm::vec3& fromUnit,
                                           const glm::vec3& toUnit) const noexcept;

private:
    glm::vec3* m_amountDeg = nullptr;

    OverlayHandler m_overlayHandler = {};

    Axis m_axis     = Axis::None;
    bool m_dragging = false;

    glm::vec3 m_origin      = glm::vec3(0.0f);             // pivot
    glm::vec3 m_startDir    = glm::vec3(1.0f, 0.0f, 0.0f); // unit dir from origin on ring plane at mouseDown
    glm::vec3 m_startAmount = glm::vec3(0.0f);

    float m_radiusW = 1.0f; // world-space ring radius (screen-sized via pixelScale)
};
