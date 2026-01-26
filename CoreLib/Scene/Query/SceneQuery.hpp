// Core/Scene/SceneQuery.hpp
#pragma once

#include <limits>
#include <vector>

class Scene;
class SceneMesh;
class Viewport;
struct MeshHit;
namespace un
{
    struct ray;
} // namespace un

/**
 * @brief Represents a hit on a mesh element.
 *
 * - For vertices and polygons, only `index` is used.
 * - For edges, both `index` (first vert) and `other` (second vert) are used.
 */
struct MeshHit
{
    MeshHit() : mesh(nullptr), dist(std::numeric_limits<float>::max()), index(-1), other(-1)
    {
    }

    SceneMesh* mesh;  ///< Scene mesh hit
    float      dist;  ///< Distance from ray origin
    int        index; ///< Primary index (vertex, poly, or edge.first)
    int        other; ///< Secondary index for edge.second (or -1)

    bool valid() const
    {
        return (mesh && index > -1);
    }
};

/**
 * @brief Abstract base class for scene hit-testing.
 *
 * Implementations can use plain CPU traversal, a custom BVH, Embree, etc.
 * Tools and UI only talk to this interface.
 */
class SceneQuery
{
public:
    virtual ~SceneQuery() = default;

    /// Rebuild any acceleration structures for the entire scene.
    virtual void rebuild(Scene* scene) = 0;

    /// Rebuild/update data for a single mesh. Default impl can be no-op.
    virtual void rebuildMesh(Scene* scene, SceneMesh* mesh) = 0;

    /// Closest vertex under ray.
    virtual MeshHit queryVert(const Viewport* vp,
                              const Scene*    scene,
                              const un::ray&  ray) const = 0;

    /// All vertices near the ray (mainly for ortho / marquee style selection).
    virtual std::vector<MeshHit> queryVerts(const Viewport* vp,
                                            const Scene*    scene,
                                            const un::ray&  ray) const = 0;

    /// Closest edge.
    virtual MeshHit queryEdge(const Viewport* vp,
                              const Scene*    scene,
                              const un::ray&  ray) const = 0;

    /// All edges near the ray.
    virtual std::vector<MeshHit> queryEdges(const Viewport* vp,
                                            const Scene*    scene,
                                            const un::ray&  ray) const = 0;

    /// Closest polygon.
    virtual MeshHit queryPoly(const Viewport* vp,
                              const Scene*    scene,
                              const un::ray&  ray) const = 0;

    /// All polygons near the ray.
    virtual std::vector<MeshHit> queryPolys(const Viewport* vp,
                                            const Scene*    scene,
                                            const un::ray&  ray) const = 0;

protected:
    SceneQuery() = default;
};
