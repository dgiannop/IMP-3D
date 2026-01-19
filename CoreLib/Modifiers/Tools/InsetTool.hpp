#pragma once

#include <span>

#include "Tool.hpp"

class Scene;
class Viewport;
class SysMesh;
struct CoreEvent;

using IndexPair = std::pair<int32_t, int32_t>;

/**
 * @class InsetTool
 * @brief Tool for insetting polygon faces (individual or grouped).
 *
 * @ingroup MeshTools
 *
 * Inset creates an inner "cap" polygon inset from the boundary and a rim
 * of polygons connecting the old boundary to the inset boundary.
 *
 * v1 notes:
 *  - Topology-only (no UV/normal map propagation yet).
 *  - Works in polygon mode via sel::to_polys(scene).
 */
class InsetTool final : public Tool
{
public:
    InsetTool();
    ~InsetTool() = default;

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

public:
    /**
     * @brief Inset the given polygons.
     * @ingroup MeshTools
     */
    static void insetPolys(SysMesh* mesh, std::span<const int32_t> polys, float amount, bool group);

private:
    float m_amount = 0.0f;
    bool  m_group  = true;
};
