//=============================================================================
// CylinderTool.hpp
//=============================================================================
#pragma once

#include <glm/glm.hpp>

#include "CylinderGizmo.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
struct CoreEvent;
class SceneMesh;

/**
 * @class CylinderTool
 * @brief Tool for interactively creating solid cylinder geometry.
 *
 * @ingroup MeshTools
 *
 * CylinderTool allows the user to define a cylinder by dragging in the viewport.
 * Adjustable parameters include radius, height, center, subdivisions (sides/segments),
 * axis orientation, and optional caps.
 *
 * UV layout (Primitives::createCylinder):
 *   - Side:       U ∈ [0, 1],     V ∈ [0.0, 0.5]
 *   - Bottom cap: packed left:    U ∈ [0.0, 0.5], V ∈ [0.5, 1.0]
 *   - Top cap:    packed right:   U ∈ [0.5, 1.0], V ∈ [0.5, 1.0]
 */
class CylinderTool final : public Tool
{
public:
    CylinderTool();
    ~CylinderTool() override = default;

    /** @copydoc Tool::activate */
    void activate(Scene* scene) override;

    /** @copydoc Tool::propertiesChanged */
    void propertiesChanged(Scene* scene) override;

    /** @copydoc Tool::mouseDown */
    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    /** @copydoc Tool::mouseDrag */
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    /** @copydoc Tool::mouseUp */
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    /** @copydoc Tool::render */
    void render(Viewport* vp, Scene* scene) override;

    /** @copydoc Tool::overlayHandler */
    OverlayHandler* overlayHandler() override;

private:
    static float clampRadius(float r) noexcept;
    static float clampHeight(float h) noexcept;

    void applyRadiusToExtents(float r) noexcept;
    void applyHeightToExtents(float h) noexcept;

    void updateDerivedParamsFromExtents() noexcept;

    void rebuildPreview(Scene* scene);

private:
    SceneMesh* m_sceneMesh = nullptr;

    float     m_radius = 0.5f;
    float     m_height = 1.0f;
    glm::vec3 m_center = glm::vec3{0.f};

    int        m_sides    = 24;
    int        m_segments = 4;
    glm::ivec3 m_axis     = glm::ivec3{0, 1, 0};
    bool       m_caps     = true;

    CylinderGizmo m_gizmo;
};
