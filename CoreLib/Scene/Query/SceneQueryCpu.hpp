#pragma once

#include <vector>

#include "SceneQuery.hpp"

class Scene;
class SceneMesh;
class Viewport;
struct MeshHit;

namespace un
{
    struct ray;
} // namespace un

/**
 * @brief CPU-based implementation of SceneQuery.
 *
 * This class uses your existing ray/geometry math on the CPU,
 * optionally with per-mesh BVHs if you already have them.
 */
class SceneQueryCpu final : public SceneQuery
{
public:
    SceneQueryCpu()           = default;
    ~SceneQueryCpu() override = default;

    void rebuild(Scene* scene) override;
    void rebuildMesh(Scene* scene, SceneMesh* mesh) override;

    MeshHit queryVert(const Viewport* vp,
                      const Scene*    scene,
                      const un::ray&  ray) const override;

    std::vector<MeshHit> queryVerts(const Viewport* vp,
                                    const Scene*    scene,
                                    const un::ray&  ray) const override;

    MeshHit queryEdge(const Viewport* vp,
                      const Scene*    scene,
                      const un::ray&  ray) const override;

    std::vector<MeshHit> queryEdges(const Viewport* vp,
                                    const Scene*    scene,
                                    const un::ray&  ray) const override;

    MeshHit queryPoly(const Viewport* vp,
                      const Scene*    scene,
                      const un::ray&  ray) const override;

    std::vector<MeshHit> queryPolys(const Viewport* vp,
                                    const Scene*    scene,
                                    const un::ray&  ray) const override;
};
