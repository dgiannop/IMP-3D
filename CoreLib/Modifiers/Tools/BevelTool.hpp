#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "Tool.hpp"

class Scene;
class Viewport;
class SysMesh;
struct CoreEvent;

using IndexPair = std::pair<int32_t, int32_t>;

/**
 * @class BevelTool
 * @brief Interactive bevel tool for edges, polygons, and vertices.
 *
 * The BevelTool provides an interactive UI layer that dispatches bevel
 * operations based on the current selection mode:
 *
 *  - Edge mode : bevel selected edges or edge loops
 *  - Poly mode : bevel polygon boundaries (grouped or per-poly)
 *  - Vert mode : bevel vertex fans
 *
 * Geometry modification is implemented in the ops layer
 * (ops::sys / ops::he); this class is responsible only for:
 *  - reading tool parameters,
 *  - reacting to selection mode,
 *  - driving interactive updates.
 *
 * Current focus:
 *  - robust, crack-free topology
 *  - stable results suitable for loop selection
 *
 * @ingroup MeshTools
 */

class BevelTool final : public Tool
{
public:
    BevelTool();
    ~BevelTool() override = default;

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

public:
    // static void bevelEdges(SysMesh* mesh, std::span<const IndexPair> edges, float width);

    // static void bevelPolys(SysMesh* mesh, std::span<int32_t> polys, float amount, bool group);

    // static void bevelVerts(SysMesh* mesh, std::span<int32_t> verts, float width);

private:
    float m_amount = 0.0f; ///< Bevel width.
    bool  m_group  = true; ///< Group behavior.
};
