#include "SceneQueryCpu.hpp"

#include "CoreTypes.hpp" // for un::ray
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SceneObject.hpp"
#include "Viewport.hpp"
// #include "CoreUtils.hpp" // if you move the math helpers there

#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    // --------------------------------------------------------
    // Math helpers (can be moved to CoreUtils.hpp later)
    // --------------------------------------------------------

    struct RayHit
    {
        bool  hit = false;
        float t   = std::numeric_limits<float>::max(); // distance along ray
    };

    RayHit rayTriangleIntersect(const un::ray&   ray,
                                const glm::vec3& v0,
                                const glm::vec3& v1,
                                const glm::vec3& v2) noexcept
    {
        constexpr float EPS = 1e-6f;

        const glm::vec3 e1  = v1 - v0;
        const glm::vec3 e2  = v2 - v0;
        const glm::vec3 p   = glm::cross(ray.dir, e2);
        const float     det = glm::dot(e1, p);

        if (std::fabs(det) < EPS)
            return {};

        const float     invDet = 1.0f / det;
        const glm::vec3 tvec   = ray.org - v0;
        const float     u      = glm::dot(tvec, p) * invDet;
        if (u < 0.0f || u > 1.0f)
            return {};

        const glm::vec3 q = glm::cross(tvec, e1);
        const float     v = glm::dot(ray.dir, q) * invDet;
        if (v < 0.0f || u + v > 1.0f)
            return {};

        const float t = glm::dot(e2, q) * invDet;
        if (t < 0.0f)
            return {};

        RayHit rh;
        rh.hit = true;
        rh.t   = t;
        return rh;
    }

    // Shortest distance between ray and segment; returns ray t and squared distance.
    struct RaySegmentHit
    {
        float tRay  = std::numeric_limits<float>::max();
        float dist2 = std::numeric_limits<float>::max();
        bool  valid = false;
    };

    RaySegmentHit closestRaySegment(const un::ray&   ray,
                                    const glm::vec3& a,
                                    const glm::vec3& b) noexcept
    {
        const glm::vec3 u  = ray.dir;
        const glm::vec3 v  = b - a;
        const glm::vec3 w0 = ray.org - a;

        const float aDot = glm::dot(u, u); // = 1 if dir normalized
        const float bDot = glm::dot(u, v);
        const float cDot = glm::dot(v, v);
        const float dDot = glm::dot(u, w0);
        const float eDot = glm::dot(v, w0);

        const float denom = aDot * cDot - bDot * bDot;
        if (std::fabs(denom) < 1e-6f)
            return {};

        float s = (bDot * eDot - cDot * dDot) / denom; // ray param
        float t = (aDot * eDot - bDot * dDot) / denom; // segment param

        if (s < 0.0f)
            s = 0.0f;

        t = std::clamp(t, 0.0f, 1.0f);

        const glm::vec3 pRay  = ray.org + s * u;
        const glm::vec3 pSeg  = a + t * v;
        const float     dist2 = glm::dot(pRay - pSeg, pRay - pSeg);

        RaySegmentHit hit;
        hit.tRay  = s;
        hit.dist2 = dist2;
        hit.valid = true;
        return hit;
    }

    // Distance from ray to point (squared), and t along ray.
    struct RayPointHit
    {
        float tRay  = std::numeric_limits<float>::max();
        float dist2 = std::numeric_limits<float>::max();
    };

    RayPointHit closestRayPoint(const un::ray&   ray,
                                const glm::vec3& p) noexcept
    {
        const glm::vec3 w = p - ray.org;
        float           t = glm::dot(w, ray.dir); // assumes dir is normalized
        if (t < 0.0f)
            t = 0.0f;

        const glm::vec3 proj  = ray.org + t * ray.dir;
        const float     dist2 = glm::dot(proj - p, proj - p);

        RayPointHit r;
        r.tRay  = t;
        r.dist2 = dist2;
        return r;
    }

    // --------------------------------------------------------
    // Mesh collection helper (same as before)
    // --------------------------------------------------------

    std::vector<SceneMesh*> collectMeshes(const Scene* scene)
    {
        std::vector<SceneMesh*> result;
        if (!scene)
            return result;

        const auto& objs = scene->sceneObjects();
        result.reserve(objs.size());
        for (const auto& obj : objs)
        {
            if (!obj)
                continue;

            if (auto* mesh = dynamic_cast<SceneMesh*>(obj.get()))
                result.push_back(mesh);
            // or: if (auto* mesh = obj->asSceneMesh()) result.push_back(mesh);
        }
        return result;
    }

    // --------------------------------------------------------
    // Per-mesh hit helpers
    // NOTE: Adjust SysMesh accessors to your real API.
    // --------------------------------------------------------

    MeshHit hitVertOnMesh(const Viewport* /*vp*/,
                          const SceneMesh* mesh,
                          const un::ray&   ray)
    {
        MeshHit best;
        if (!mesh)
            return best;

        const SysMesh* sys = mesh->sysMesh(); // <-- adjust if name differs
        if (!sys)
            return best;

        // You might want a tolerance based on viewport pixel scale:
        // float tolWorld = vp->pixelScale() * someFactor;
        // For now, just use a fixed world-space radius:
        const float tolWorld = 0.05f;
        const float tol2     = tolWorld * tolWorld;

        const int vertCount = sys->num_verts(); // <-- adjust to your API
        for (int vi = 0; vi < vertCount; ++vi)
        {
            if (!sys->vert_valid(vi)) // <-- if you have vert_valid
                continue;

            // Assuming SysMesh has vert_pos(int) -> glm::vec3
            const glm::vec3 p = sys->vert_position(vi); // <-- adjust

            RayPointHit rp = closestRayPoint(ray, p);
            if (rp.dist2 > tol2)
                continue;

            if (rp.tRay < best.dist)
            {
                best.mesh  = const_cast<SceneMesh*>(mesh);
                best.dist  = rp.tRay;
                best.index = vi;
                best.other = -1;
            }
        }

        return best;
    }

    std::vector<MeshHit> hitVertsOnMesh(const Viewport* /*vp*/,
                                        const SceneMesh* mesh,
                                        const un::ray&   ray)
    {
        std::vector<MeshHit> result;
        if (!mesh)
            return result;

        const SysMesh* sys = mesh->sysMesh();
        if (!sys)
            return result;

        const float tolWorld = 0.05f;
        const float tol2     = tolWorld * tolWorld;

        const int vertCount = sys->num_verts(); // <-- adjust
        result.reserve(vertCount / 4);

        for (int vi = 0; vi < vertCount; ++vi)
        {
            if (!sys->vert_valid(vi)) // <-- adjust/remove if not present
                continue;

            const glm::vec3 p = sys->vert_position(vi); // <-- adjust

            RayPointHit rp = closestRayPoint(ray, p);
            if (rp.dist2 > tol2)
                continue;

            MeshHit h;
            h.mesh  = const_cast<SceneMesh*>(mesh);
            h.dist  = rp.tRay;
            h.index = vi;
            h.other = -1;
            result.push_back(h);
        }

        return result;
    }

    MeshHit hitEdgeOnMesh(const Viewport* /*vp*/,
                          const SceneMesh* mesh,
                          const un::ray&   ray)
    {
        MeshHit best;
        if (!mesh)
            return best;

        const SysMesh* sys = mesh->sysMesh();
        if (!sys)
            return best;

        const float tolWorld = 0.05f;
        const float tol2     = tolWorld * tolWorld;

        for (IndexPair edge : sys->all_edges())
        {
            if (!sys->vert_valid(edge.first) || !sys->vert_valid(edge.second))
                continue;

            const glm::vec3 a = sys->vert_position(edge.first);  // <-- adjust
            const glm::vec3 b = sys->vert_position(edge.second); // <-- adjust

            RaySegmentHit rs = closestRaySegment(ray, a, b);
            if (!rs.valid || rs.dist2 > tol2)
                continue;

            if (rs.tRay < best.dist)
            {
                best.mesh  = const_cast<SceneMesh*>(mesh);
                best.dist  = rs.tRay;
                best.index = edge.first;
                best.other = edge.second;
            }
        }

        return best;
    }

    std::vector<MeshHit> hitEdgesOnMesh(const Viewport* /*vp*/,
                                        const SceneMesh* mesh,
                                        const un::ray&   ray)
    {
        std::vector<MeshHit> result;
        if (!mesh)
            return result;

        const SysMesh* sys = mesh->sysMesh();
        if (!sys)
            return result;

        const float tolWorld = 0.05f;
        const float tol2     = tolWorld * tolWorld;

        for (IndexPair edge : sys->all_edges())
        {
            if (!sys->vert_valid(edge.first) || !sys->vert_valid(edge.second))
                continue;

            const glm::vec3 a = sys->vert_position(edge.first);  // <-- adjust
            const glm::vec3 b = sys->vert_position(edge.second); // <-- adjust

            RaySegmentHit rs = closestRaySegment(ray, a, b);
            if (!rs.valid || rs.dist2 > tol2)
                continue;

            MeshHit h;
            h.mesh  = const_cast<SceneMesh*>(mesh);
            h.dist  = rs.tRay;
            h.index = edge.first;
            h.other = edge.second;
            result.push_back(h);
        }

        return result;
    }

    MeshHit hitPolyOnMesh(const Viewport* /*vp*/,
                          const SceneMesh* mesh,
                          const un::ray&   ray)
    {
        MeshHit best;
        if (!mesh)
            return best;

        const SysMesh* sys = mesh->sysMesh();
        if (!sys)
            return best;

        const int polyCount = sys->num_polys(); // <-- adjust

        for (int pi = 0; pi < polyCount; ++pi)
        {
            if (!sys->poly_valid(pi)) // <-- if you have this
                continue;

            // Assuming poly_verts(pi) returns something vector/span-like of vert indices.
            const auto verts = sys->poly_verts(pi); // <-- adjust

            if (verts.size() < 3)
                continue;

            // Fan triangulation: (v0, v[i], v[i+1])
            const glm::vec3 v0 = sys->vert_position(verts[0]); // <-- adjust

            for (std::size_t i = 1; i + 1 < verts.size(); ++i)
            {
                const glm::vec3 v1 = sys->vert_position(verts[i]);     // <-- adjust
                const glm::vec3 v2 = sys->vert_position(verts[i + 1]); // <-- adjust

                RayHit rh = rayTriangleIntersect(ray, v0, v1, v2);
                if (!rh.hit)
                    continue;

                if (rh.t < best.dist)
                {
                    best.mesh  = const_cast<SceneMesh*>(mesh);
                    best.dist  = rh.t;
                    best.index = pi; // polygon index
                    best.other = -1;
                }
            }
        }

        return best;
    }

    std::vector<MeshHit> hitPolysOnMesh(const Viewport* /*vp*/,
                                        const SceneMesh* mesh,
                                        const un::ray&   ray)
    {
        std::vector<MeshHit> result;
        if (!mesh)
            return result;

        const SysMesh* sys = mesh->sysMesh();
        if (!sys)
            return result;

        const int polyCount = sys->num_polys(); // <-- adjust
        result.reserve(polyCount / 4);

        for (int pi = 0; pi < polyCount; ++pi)
        {
            if (!sys->poly_valid(pi)) // <-- if you have this
                continue;

            const auto verts = sys->poly_verts(pi); // <-- adjust
            if (verts.size() < 3)
                continue;

            const glm::vec3 v0     = sys->vert_position(verts[0]); // <-- adjust
            bool            anyHit = false;
            float           bestT  = std::numeric_limits<float>::max();

            for (std::size_t i = 1; i + 1 < verts.size(); ++i)
            {
                const glm::vec3 v1 = sys->vert_position(verts[i]);     // <-- adjust
                const glm::vec3 v2 = sys->vert_position(verts[i + 1]); // <-- adjust

                RayHit rh = rayTriangleIntersect(ray, v0, v1, v2);
                if (!rh.hit)
                    continue;

                anyHit = true;
                if (rh.t < bestT)
                    bestT = rh.t;
            }

            if (anyHit)
            {
                MeshHit h;
                h.mesh  = const_cast<SceneMesh*>(mesh);
                h.dist  = bestT;
                h.index = pi;
                h.other = -1;
                result.push_back(h);
            }
        }

        return result;
    }

} // anonymous namespace

// ==================================================================
// SceneQueryCpu methods (unchanged except using MeshHit::valid())
// ==================================================================

void SceneQueryCpu::rebuild(Scene* /*scene*/)
{
    // No cached structures yet.
}

void SceneQueryCpu::rebuildMesh(Scene* /*scene*/, SceneMesh* /*mesh*/)
{
    // No per-mesh cache yet.
}

// Vertices
MeshHit SceneQueryCpu::queryVert(const Viewport* vp,
                                 const Scene*    scene,
                                 const un::ray&  ray) const
{
    MeshHit    best;
    const auto meshes = collectMeshes(scene);

    for (const SceneMesh* mesh : meshes)
    {
        MeshHit h = hitVertOnMesh(vp, mesh, ray);
        if (h.valid() && (!best.valid() || h.dist < best.dist))
            best = h;
    }
    return best;
}

std::vector<MeshHit> SceneQueryCpu::queryVerts(const Viewport* vp,
                                               const Scene*    scene,
                                               const un::ray&  ray) const
{
    std::vector<MeshHit> result;
    const auto           meshes = collectMeshes(scene);

    for (const SceneMesh* mesh : meshes)
    {
        auto hits = hitVertsOnMesh(vp, mesh, ray);
        result.insert(result.end(), hits.begin(), hits.end());
    }
    return result;
}

// Edges
MeshHit SceneQueryCpu::queryEdge(const Viewport* vp,
                                 const Scene*    scene,
                                 const un::ray&  ray) const
{
    MeshHit    best;
    const auto meshes = collectMeshes(scene);

    for (const SceneMesh* mesh : meshes)
    {
        MeshHit h = hitEdgeOnMesh(vp, mesh, ray);
        if (h.valid() && (!best.valid() || h.dist < best.dist))
            best = h;
    }
    return best;
}

std::vector<MeshHit> SceneQueryCpu::queryEdges(const Viewport* vp,
                                               const Scene*    scene,
                                               const un::ray&  ray) const
{
    std::vector<MeshHit> result;
    const auto           meshes = collectMeshes(scene);

    for (const SceneMesh* mesh : meshes)
    {
        auto hits = hitEdgesOnMesh(vp, mesh, ray);
        result.insert(result.end(), hits.begin(), hits.end());
    }
    return result;
}

// Polygons
MeshHit SceneQueryCpu::queryPoly(const Viewport* vp,
                                 const Scene*    scene,
                                 const un::ray&  ray) const
{
    MeshHit    best;
    const auto meshes = collectMeshes(scene);

    for (const SceneMesh* mesh : meshes)
    {
        MeshHit h = hitPolyOnMesh(vp, mesh, ray);
        if (h.valid() && (!best.valid() || h.dist < best.dist))
            best = h;
    }
    return best;
}

std::vector<MeshHit> SceneQueryCpu::queryPolys(const Viewport* vp,
                                               const Scene*    scene,
                                               const un::ray&  ray) const
{
    std::vector<MeshHit> result;
    const auto           meshes = collectMeshes(scene);

    for (const SceneMesh* mesh : meshes)
    {
        auto hits = hitPolysOnMesh(vp, mesh, ray);
        result.insert(result.end(), hits.begin(), hits.end());
    }
    return result;
}
