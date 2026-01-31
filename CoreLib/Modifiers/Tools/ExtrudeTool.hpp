#pragma once

#include <span>

#include "NormalPullGizmo.hpp"
#include "Tool.hpp"

class Scene;
class Viewport;
class SysMesh;
class OverlayHandler;
struct CoreEvent;

using IndexPair = std::pair<int32_t, int32_t>;

/**
 * @class ExtrudeTool
 * @brief Tool for interactively extruding faces, edges, or vertices in a mesh.
 *
 * @ingroup MeshTools
 *
 * ExtrudeTool performs interactive mesh extrusion operations, allowing the user
 * to pull geometry outward or inward along normals or other tool-driven directions.
 */
class ExtrudeTool final : public Tool
{
public:
    ExtrudeTool();
    ~ExtrudeTool() = default;

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

public:
    /**
     * @brief Extrude the given polygons.
     * @ingroup MeshTools
     */
    static void extrudePolys(SysMesh* mesh, std::span<const int32_t> polys, float amount, bool group);

    /**
     * @brief Extrude a set of vertices.
     * @ingroup MeshTools
     */
    static void extrudeVerts(SysMesh* mesh, std::span<const int32_t> verts, float amount, bool group);

    /**
     * @brief Extrude the specified edges.
     * @ingroup MeshTools
     */
    static void extrudeEdges(SysMesh* mesh, std::span<const IndexPair> edges, float amount);

private:
    float m_amount = 0.f;  ///< Current extrusion amount.
    bool  m_group  = true; ///< Whether extrusion operates as a connected group.

    NormalPullGizmo m_gizmo{&m_amount};
};
