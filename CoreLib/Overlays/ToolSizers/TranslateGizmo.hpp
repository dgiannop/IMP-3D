#pragma once

#include <glm/glm.hpp>
#include <string>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief World-axis XYZ translate gizmo.
 *
 * Owns overlay + picking and writes to the tool's parameters (amount).
 * Tool calls propertiesChanged() after changes to apply to meshes.
 *
 * Common usage:
 *  - TranslateGizmo gizmo{&m_amount};
 *  - gizmo.mouseDown/Drag/Up(...)
 *  - tool.propertiesChanged(scene) rebuilds / moves using m_amount
 */
class TranslateGizmo final
{
public:
    explicit TranslateGizmo(glm::vec3* amount);

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

    static Axis      axisFromPickName(const std::string& s) noexcept;

    static glm::vec3 axisDir(Axis a) noexcept;

    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 Axis             axis,
                                                 float            mx,
                                                 float            my) const;

private:
    glm::vec3* m_amount = nullptr; ///< Tool-owned delta (world)

    OverlayHandler m_overlayHandler = {};

    Axis m_axis     = Axis::None;
    bool m_dragging = false;

    glm::vec3 m_origin  = glm::vec3{0.0f}; ///< selection center at drag start
    glm::vec3 m_baseOrigin = glm::vec3{0.0f};
    glm::vec3 m_axisDir = glm::vec3{1.0f, 0.0f, 0.0f};

    glm::vec3 m_startOnPlane = glm::vec3{0.0f};
    glm::vec3 m_startAmount  = glm::vec3{0.0f}; ///< amount at mouseDown (for stable absolute dragging)

    float m_length = 1.5f;
};
