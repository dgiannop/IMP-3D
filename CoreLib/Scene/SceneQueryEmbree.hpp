#pragma once

#include <memory>
#include <vector>

#include "CoreTypes.hpp"    // for un::ray
#include "SceneQuery.hpp"   // for MeshHit base interface
#include "embree4/rtcore.h" // RTCDevice, RTCScene, etc.

class Scene;
class SceneMesh;
class Viewport;

class SceneQueryEmbree : public SceneQuery
{
public:
    SceneQueryEmbree();
    ~SceneQueryEmbree() override;

    void rebuild(Scene* scene) override;
    void rebuildMesh(Scene* scene, SceneMesh* mesh) override;

    // Single-hit
    MeshHit queryVert(const Viewport* vp,
                      const Scene*    scene,
                      const un::ray&  ray) const override;

    MeshHit queryEdge(const Viewport* vp,
                      const Scene*    scene,
                      const un::ray&  ray) const override;

    MeshHit queryPoly(const Viewport* vp,
                      const Scene*    scene,
                      const un::ray&  ray) const override;

    // Multi-hit
    std::vector<MeshHit> queryVerts(const Viewport* vp,
                                    const Scene*    scene,
                                    const un::ray&  ray) const override;

    std::vector<MeshHit> queryEdges(const Viewport* vp,
                                    const Scene*    scene,
                                    const un::ray&  ray) const override;

    std::vector<MeshHit> queryPolys(const Viewport* vp,
                                    const Scene*    scene,
                                    const un::ray&  ray) const override;

private:
    struct MeshAccel;

    RTCDevice              m_device   = nullptr;
    RTCScene               m_rtcScene = nullptr; // Single Embree scene for all meshes
    std::vector<MeshAccel> m_meshes;             // Indexed by geomId

    void buildForScene(Scene* scene);
};
