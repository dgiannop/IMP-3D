//=============================================================================
// PrimitiveGizmo.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "OverlayHandler.hpp"

class Scene;
class Viewport;
struct CoreEvent;

/**
 * @brief Parametric gizmo for primitive creation/editing (center + 6 face handles).
 *
 * Drives:
 *  - center: translation
 *  - size:   per-axis face resize (push/pull a single face; opposite face stays fixed)
 *
 * Handles:
 *  - 0: Center move
 *  - 1: +X, 2: -X
 *  - 3: +Y, 4: -Y
 *  - 5: +Z, 6: -Z
 */
class PrimitiveGizmo final
{
public:
    PrimitiveGizmo(glm::vec3* center, glm::vec3* size);

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev);
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& ev);

    void render(Viewport* vp, Scene* scene);

    OverlayHandler&       overlayHandler() noexcept { return m_overlayHandler; }
    const OverlayHandler& overlayHandler() const noexcept { return m_overlayHandler; }

private:
    enum class Mode : int32_t
    {
        None   = -1,
        Center = 0,

        PosX = 1,
        NegX = 2,

        PosY = 3,
        NegY = 4,

        PosZ = 5,
        NegZ = 6,
    };

    static Mode modeFromHandle(int32_t h) noexcept
    {
        switch (h)
        {
            case 0:
                return Mode::Center;
            case 1:
                return Mode::PosX;
            case 2:
                return Mode::NegX;
            case 3:
                return Mode::PosY;
            case 4:
                return Mode::NegY;
            case 5:
                return Mode::PosZ;
            case 6:
                return Mode::NegZ;
            default:
                return Mode::None;
        }
    }

    static glm::vec3 axisDir(Mode m) noexcept;
    static int32_t   axisIndex(Mode m) noexcept; // 0=x, 1=y, 2=z, -1=none

    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept;

    [[nodiscard]] glm::vec3 dragPointOnAxisPlane(Viewport*        vp,
                                                 const glm::vec3& origin,
                                                 const glm::vec3& axisDir,
                                                 float            mx,
                                                 float            my) const;

    void buildBillboardSquare(Viewport*        vp,
                              const glm::vec3& center,
                              float            halfExtentWorld,
                              const glm::vec4& color,
                              bool             filledForPick);

    void applyFaceDragDelta(float delta);

private:
    glm::vec3* m_center = nullptr; ///< Tool-owned primitive center
    glm::vec3* m_size   = nullptr; ///< Tool-owned primitive size (full extents)

    OverlayHandler m_overlayHandler = {};

    Mode m_mode     = Mode::None;
    bool m_dragging = false;

    // Drag state
    glm::vec3 m_startCenter = glm::vec3{0.0f};
    glm::vec3 m_startSize   = glm::vec3{1.0f};

    glm::vec3 m_axis       = glm::vec3{0.0f}; // signed axis dir for current face
    int32_t   m_axisIdx    = -1;              // 0/1/2
    float     m_startParam = 0.0f;            // param along axis at mouseDown (for face)

    // Center drag anchor (screen space)
    float m_startMx = 0.0f;
    float m_startMy = 0.0f;

    // Render tuning (world units at center, derived from pixelScale)
    float m_centerHalfWorld = 0.02f;
    float m_axisLenWorld    = 0.2f;
    float m_tipRadiusWorld  = 0.015f;

    // Min size clamp per axis
    float m_minSize = 0.0001f;

    // Frozen billboard basis during drag (prevents flip/flicker).
    glm::vec3 m_bbRight = glm::vec3{1.0f, 0.0f, 0.0f};
    glm::vec3 m_bbUp    = glm::vec3{0.0f, 1.0f, 0.0f};

    // Drag: freeze a reference basis to prevent sign flips.
    bool      m_hasDragBasis = false;
    glm::vec3 m_dragRefRight = glm::vec3{1.0f, 0.0f, 0.0f};
    glm::vec3 m_dragRefUp    = glm::vec3{0.0f, 1.0f, 0.0f};
};
