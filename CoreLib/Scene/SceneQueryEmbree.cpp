#include "SceneQueryEmbree.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <embree4/rtcore.h>
#include <embree4/rtcore_ray.h>
#include <glm/glm.hpp>
#include <iostream>
#include <limits>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "Viewport.hpp"

// --------------------------------------------------------
// Internal helpers
// --------------------------------------------------------

namespace
{
    constexpr float kTolWorld = 0.05f;
    constexpr float kTol2     = kTolWorld * kTolWorld;

    struct RaySegmentHit
    {
        float tRay  = std::numeric_limits<float>::max();
        float dist2 = std::numeric_limits<float>::max();
        bool  valid = false;
    };

    RaySegmentHit closestRaySegment(const un::ray& ray, const glm::vec3& a, const glm::vec3& b) noexcept
    {
        const glm::vec3 u  = ray.dir;
        const glm::vec3 v  = b - a;
        const glm::vec3 w0 = ray.org - a;

        const float aDot = glm::dot(u, u);
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

    struct RayPointHit
    {
        float tRay  = std::numeric_limits<float>::max();
        float dist2 = std::numeric_limits<float>::max();
    };

    RayPointHit closestRayPoint(const un::ray& ray, const glm::vec3& p) noexcept
    {
        const glm::vec3 w = p - ray.org;
        float           t = glm::dot(w, ray.dir);
        if (t < 0.0f)
            t = 0.0f;

        const glm::vec3 proj  = ray.org + t * ray.dir;
        const float     dist2 = glm::dot(proj - p, proj - p);

        RayPointHit r;
        r.tRay  = t;
        r.dist2 = dist2;
        return r;
    }

    // Multi-hit Embree helper: repeatedly intersect and advance tnear.
    struct TriHit
    {
        unsigned int geomId = RTC_INVALID_GEOMETRY_ID;
        unsigned int primId = RTC_INVALID_GEOMETRY_ID;
        float        t      = std::numeric_limits<float>::max();
    };

    std::vector<TriHit> intersectAllTriangles(RTCScene scene, const un::ray& ray)
    {
        std::vector<TriHit> hits;
        if (!scene)
            return hits;

        constexpr float tMax = std::numeric_limits<float>::max();
        constexpr float eps  = 1e-4f;

        float tnear = 0.0f;

        while (true)
        {
            RTCRayHit rh{};
            rh.ray.org_x = ray.org.x;
            rh.ray.org_y = ray.org.y;
            rh.ray.org_z = ray.org.z;

            rh.ray.dir_x = ray.dir.x;
            rh.ray.dir_y = ray.dir.y;
            rh.ray.dir_z = ray.dir.z;

            rh.ray.tnear = tnear;
            rh.ray.tfar  = tMax;
            rh.ray.mask  = 0xFFFFFFFFu;
            rh.ray.flags = 0;

            rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
            rh.hit.primID = RTC_INVALID_GEOMETRY_ID;

            RTCIntersectArguments args;
            rtcInitIntersectArguments(&args);

            rtcIntersect1(scene, &rh, &args);

            if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID ||
                rh.hit.primID == RTC_INVALID_GEOMETRY_ID)
            {
                break;
            }

            hits.push_back(TriHit{
                rh.hit.geomID,
                rh.hit.primID,
                rh.ray.tfar,
            });

            tnear = rh.ray.tfar + eps;
            if (tnear >= tMax)
                break;
        }

        return hits;
    }

    bool isPolygonBoundaryEdge(const SysMesh* sys, int polyIndex, int a, int b) noexcept
    {
        if (!sys || !sys->poly_valid(polyIndex))
            return false;

        const auto        verts = sys->poly_verts(polyIndex);
        const std::size_t n     = verts.size();
        if (n < 2)
            return false;

        for (std::size_t i = 0; i < n; ++i)
        {
            const int v0 = verts[i];
            const int v1 = verts[(i + 1) % n]; // wrap-around

            if ((v0 == a && v1 == b) || (v0 == b && v1 == a))
                return true;
        }

        return false;
    }

    RTCRayHit fromRay(const un::ray& ray)
    {
        RTCRayHit rh{};
        rh.ray.org_x = ray.org.x;
        rh.ray.org_y = ray.org.y;
        rh.ray.org_z = ray.org.z;

        rh.ray.dir_x = ray.dir.x;
        rh.ray.dir_y = ray.dir.y;
        rh.ray.dir_z = ray.dir.z;

        rh.ray.tnear = 0.0f;
        rh.ray.tfar  = std::numeric_limits<float>::max();
        rh.ray.mask  = 0xFFFFFFFFu;
        rh.ray.flags = 0;

        rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        rh.hit.primID = RTC_INVALID_GEOMETRY_ID;
        return rh;
    }

} // namespace

// --------------------------------------------------------
// MeshAccel for per-geometry mapping (geomId -> SceneMesh + tri mapping)
// --------------------------------------------------------

struct SceneQueryEmbree::MeshAccel
{
    SceneMesh*                      owner = nullptr;
    std::vector<int>                triToPoly;  // primID -> poly index
    std::vector<std::array<int, 3>> triToVerts; // primID -> (v0,v1,v2)
    unsigned int                    geomId = RTC_INVALID_GEOMETRY_ID;

    MeshAccel() = default;

    MeshAccel(const MeshAccel&)            = delete;
    MeshAccel& operator=(const MeshAccel&) = delete;

    MeshAccel(MeshAccel&& other) noexcept
        : owner(other.owner),
          triToPoly(std::move(other.triToPoly)),
          triToVerts(std::move(other.triToVerts)),
          geomId(other.geomId)
    {
        other.owner  = nullptr;
        other.geomId = RTC_INVALID_GEOMETRY_ID;
    }

    MeshAccel& operator=(MeshAccel&& other) noexcept
    {
        if (this != &other)
        {
            owner      = other.owner;
            triToPoly  = std::move(other.triToPoly);
            triToVerts = std::move(other.triToVerts);
            geomId     = other.geomId;

            other.owner  = nullptr;
            other.geomId = RTC_INVALID_GEOMETRY_ID;
        }
        return *this;
    }

    ~MeshAccel() = default;
};

// --------------------------------------------------------
// SceneQueryEmbree implementation
// --------------------------------------------------------

SceneQueryEmbree::SceneQueryEmbree()
{
    m_device = rtcNewDevice(nullptr); // nullptr = default config

    if (!m_device)
        return;

    const auto tasking = rtcGetDeviceProperty(m_device, RTC_DEVICE_PROPERTY_TASKING_SYSTEM);

    if (tasking == 1)
    {
        std::cerr << "Embree tasking system: TBB\n";
    }
    else if (tasking == 0)
    {
        std::cerr << "Embree tasking system: internal\n";
    }
    else if (tasking == 2)
    {
        std::cerr << "Embree tasking system: PPL\n";
    }
}

SceneQueryEmbree::~SceneQueryEmbree()
{
    if (m_rtcScene)
        rtcReleaseScene(m_rtcScene);

    if (m_device)
        rtcReleaseDevice(m_device);
}

void SceneQueryEmbree::buildForScene(Scene* scene)
{
    m_meshes.clear();

    if (m_rtcScene)
    {
        rtcReleaseScene(m_rtcScene);
        m_rtcScene = nullptr;
    }

    if (!scene || !m_device)
        return;

    m_rtcScene = rtcNewScene(m_device);
    rtcSetSceneBuildQuality(m_rtcScene, RTC_BUILD_QUALITY_MEDIUM);

    const auto& objs = scene->sceneObjects();

    for (const auto& obj : objs)
    {
        auto* mesh = dynamic_cast<SceneMesh*>(obj.get());
        if (!mesh)
            continue;

        const SysMesh* sys = mesh->sysMesh();
        if (!sys)
            continue;

        // IMPORTANT:
        // SysMesh supports holes. num_*() is a COUNT of valid elements, NOT the index range.
        // For Embree buffers (indexed by raw SysMesh indices), we must use *_buffer_size().
        const int vertCount = static_cast<int>(sys->vert_buffer_size());
        const int polyCount = static_cast<int>(sys->poly_buffer_size());

        if (vertCount == 0 || polyCount == 0)
            continue;

        // Count triangles via fan triangulation of each poly.
        int triCount = 0;
        for (int pi = 0; pi < polyCount; ++pi)
        {
            if (!sys->poly_valid(pi))
                continue;

            const auto verts = sys->poly_verts(pi);
            if (verts.size() >= 3)
                triCount += static_cast<int>(verts.size()) - 2;
        }
        if (triCount == 0)
            continue;

        RTCGeometry geom = rtcNewGeometry(m_device, RTC_GEOMETRY_TYPE_TRIANGLE);
        rtcSetGeometryBuildQuality(geom, RTC_BUILD_QUALITY_MEDIUM);

        // Vertex buffer
        struct RTCFloat3
        {
            float x, y, z;
        };

        auto* vbuf = reinterpret_cast<RTCFloat3*>(
            rtcSetNewGeometryBuffer(geom,
                                    RTC_BUFFER_TYPE_VERTEX,
                                    0,
                                    RTC_FORMAT_FLOAT3,
                                    sizeof(RTCFloat3),
                                    vertCount));

        for (int vi = 0; vi < vertCount; ++vi)
        {
            glm::vec3 p{0.0f};
            if (sys->vert_valid(vi))
                p = sys->vert_position(vi);

            vbuf[vi].x = p.x;
            vbuf[vi].y = p.y;
            vbuf[vi].z = p.z;
        }

        // Index buffer
        struct RTCTri
        {
            unsigned int v0, v1, v2;
        };

        auto* ibuf = reinterpret_cast<RTCTri*>(
            rtcSetNewGeometryBuffer(geom,
                                    RTC_BUFFER_TYPE_INDEX,
                                    0,
                                    RTC_FORMAT_UINT3,
                                    sizeof(RTCTri),
                                    triCount));

        std::vector<int>                triToPoly;
        std::vector<std::array<int, 3>> triToVerts;
        triToPoly.reserve(triCount);
        triToVerts.reserve(triCount);

        int triIndex = 0;
        for (int pi = 0; pi < polyCount; ++pi)
        {
            if (!sys->poly_valid(pi))
                continue;

            const auto verts = sys->poly_verts(pi);
            if (verts.size() < 3)
                continue;

            const unsigned int v0 = static_cast<unsigned int>(verts[0]);

            for (std::size_t i = 1; i + 1 < verts.size(); ++i)
            {
                const unsigned int v1 = static_cast<unsigned int>(verts[i]);
                const unsigned int v2 = static_cast<unsigned int>(verts[i + 1]);

                ibuf[triIndex].v0 = v0;
                ibuf[triIndex].v1 = v1;
                ibuf[triIndex].v2 = v2;

                triToPoly.push_back(pi);
                triToVerts.push_back(
                    {static_cast<int>(v0),
                     static_cast<int>(v1),
                     static_cast<int>(v2)});

                ++triIndex;
            }
        }

        rtcCommitGeometry(geom);
        unsigned int geomId = rtcAttachGeometry(m_rtcScene, geom);
        rtcReleaseGeometry(geom);

        if (geomId >= m_meshes.size())
            m_meshes.resize(geomId + 1);

        MeshAccel accel;
        accel.owner      = mesh;
        accel.triToPoly  = std::move(triToPoly);
        accel.triToVerts = std::move(triToVerts);
        accel.geomId     = geomId;

        m_meshes[geomId] = std::move(accel);
    }

    rtcCommitScene(m_rtcScene);
}

void SceneQueryEmbree::rebuild(Scene* scene)
{
    buildForScene(scene);
}

void SceneQueryEmbree::rebuildMesh(Scene* scene, SceneMesh* /*mesh*/)
{
    // Simple version: rebuild everything.
    buildForScene(scene);
}

// --------------------------------------------------------
// Vertices – closest hit via Embree
// --------------------------------------------------------

MeshHit SceneQueryEmbree::queryVert(const Viewport* /*vp*/,
                                    const Scene* /*scene*/,
                                    const un::ray& ray) const
{
    MeshHit best;

    if (!m_device || !m_rtcScene)
        return best;

    RTCRayHit rh = fromRay(ray);

    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);

    rtcIntersect1(m_rtcScene, &rh, &args);

    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID ||
        rh.hit.primID == RTC_INVALID_GEOMETRY_ID)
        return best;

    const unsigned int geomId = rh.hit.geomID;
    const unsigned int primId = rh.hit.primID;

    if (geomId >= m_meshes.size())
        return best;

    const MeshAccel& accel = m_meshes[geomId];
    if (!accel.owner)
        return best;

    if (primId >= accel.triToVerts.size())
        return best;

    const SysMesh* sys = accel.owner->sysMesh();
    if (!sys)
        return best;

    const auto& triVerts = accel.triToVerts[primId];

    for (int k = 0; k < 3; ++k)
    {
        const int vi = triVerts[k];
        if (!sys->vert_valid(vi))
            continue;

        const glm::vec3 p  = sys->vert_position(vi);
        RayPointHit     rp = closestRayPoint(ray, p);
        if (rp.dist2 > kTol2)
            continue;

        if (!best.valid() || rp.tRay < best.dist)
        {
            best.mesh  = accel.owner;
            best.dist  = rp.tRay;
            best.index = vi;
            best.other = -1;
        }
    }

    return best;
}

// --------------------------------------------------------
// Edges – closest hit via Embree (boundary edges only)
// --------------------------------------------------------

MeshHit SceneQueryEmbree::queryEdge(const Viewport* /*vp*/,
                                    const Scene* /*scene*/,
                                    const un::ray& ray) const
{
    MeshHit best;

    if (!m_device || !m_rtcScene)
        return best;

    RTCRayHit rh = fromRay(ray);

    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);

    rtcIntersect1(m_rtcScene, &rh, &args);

    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID ||
        rh.hit.primID == RTC_INVALID_GEOMETRY_ID)
        return best;

    const unsigned int geomId = rh.hit.geomID;
    const unsigned int primId = rh.hit.primID;

    if (geomId >= m_meshes.size())
        return best;

    const MeshAccel& accel = m_meshes[geomId];
    if (!accel.owner)
        return best;

    if (primId >= accel.triToVerts.size() ||
        primId >= accel.triToPoly.size())
        return best;

    const SysMesh* sys = accel.owner->sysMesh();
    if (!sys)
        return best;

    const auto& triVerts  = accel.triToVerts[primId];
    const int   polyIndex = accel.triToPoly[primId];

    const int v0 = triVerts[0];
    const int v1 = triVerts[1];
    const int v2 = triVerts[2];

    const int edgePairs[3][2] = {
        {v0, v1},
        {v1, v2},
        {v2, v0},
    };

    for (int e = 0; e < 3; ++e)
    {
        const int aIdx = edgePairs[e][0];
        const int bIdx = edgePairs[e][1];

        // Skip non-boundary edges (triangulation diagonals)
        if (!isPolygonBoundaryEdge(sys, polyIndex, aIdx, bIdx))
            continue;

        if (!sys->vert_valid(aIdx) || !sys->vert_valid(bIdx))
            continue;

        const glm::vec3 a = sys->vert_position(aIdx);
        const glm::vec3 b = sys->vert_position(bIdx);

        RaySegmentHit rs = closestRaySegment(ray, a, b);
        if (!rs.valid || rs.dist2 > kTol2)
            continue;

        if (!best.valid() || rs.tRay < best.dist)
        {
            best.mesh  = accel.owner;
            best.dist  = rs.tRay;
            best.index = aIdx;
            best.other = bIdx;
        }
    }

    return best;
}

// --------------------------------------------------------
// Polygons – closest hit via Embree
// --------------------------------------------------------

MeshHit SceneQueryEmbree::queryPoly(const Viewport* /*vp*/,
                                    const Scene* /*scene*/,
                                    const un::ray& ray) const
{
    MeshHit best;

    if (!m_device || !m_rtcScene)
        return best;

    RTCRayHit rh = fromRay(ray);

    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);

    rtcIntersect1(m_rtcScene, &rh, &args);

    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID ||
        rh.hit.primID == RTC_INVALID_GEOMETRY_ID)
        return best;

    const unsigned int geomId = rh.hit.geomID;
    const unsigned int primId = rh.hit.primID;

    if (geomId >= m_meshes.size())
        return best;

    const MeshAccel& accel = m_meshes[geomId];
    if (!accel.owner)
        return best;

    if (primId >= accel.triToPoly.size())
        return best;

    const int polyIndex = accel.triToPoly[primId];

    best.mesh  = accel.owner;
    best.dist  = rh.ray.tfar;
    best.index = polyIndex;
    best.other = -1;

    return best;
}

// --------------------------------------------------------
// Multi-hit – Embree-based
// --------------------------------------------------------

std::vector<MeshHit> SceneQueryEmbree::queryVerts(const Viewport* /*vp*/,
                                                  const Scene* /*scene*/,
                                                  const un::ray& ray) const
{
    std::vector<MeshHit> result;

    if (!m_device || !m_rtcScene)
        return result;

    const auto triHits = intersectAllTriangles(m_rtcScene, ray);
    if (triHits.empty())
        return result;

    auto alreadyHave = [&](SceneMesh* owner, int vi) {
        for (const MeshHit& h : result)
        {
            if (h.mesh == owner && h.index == vi && h.other == -1)
                return true;
        }
        return false;
    };

    for (const TriHit& th : triHits)
    {
        if (th.geomId >= m_meshes.size())
            continue;

        const MeshAccel& accel = m_meshes[th.geomId];
        if (!accel.owner)
            continue;

        if (th.primId >= accel.triToVerts.size())
            continue;

        const SysMesh* sys = accel.owner->sysMesh();
        if (!sys)
            continue;

        const auto& triVerts = accel.triToVerts[th.primId];

        for (int k = 0; k < 3; ++k)
        {
            const int vi = triVerts[k];
            if (!sys->vert_valid(vi))
                continue;

            if (alreadyHave(accel.owner, vi))
                continue;

            const glm::vec3 p  = sys->vert_position(vi);
            RayPointHit     rp = closestRayPoint(ray, p);
            if (rp.dist2 > kTol2)
                continue;

            MeshHit h;
            h.mesh  = accel.owner;
            h.dist  = rp.tRay;
            h.index = vi;
            h.other = -1;
            result.push_back(h);
        }
    }

    return result;
}

std::vector<MeshHit> SceneQueryEmbree::queryEdges(const Viewport* /*vp*/,
                                                  const Scene* /*scene*/,
                                                  const un::ray& ray) const
{
    std::vector<MeshHit> result;

    if (!m_device || !m_rtcScene)
        return result;

    const auto triHits = intersectAllTriangles(m_rtcScene, ray);
    if (triHits.empty())
        return result;

    auto canonicalPair = [](int a, int b) {
        if (a > b)
            std::swap(a, b);
        return std::pair<int, int>(a, b);
    };

    std::vector<std::pair<SceneMesh*, std::pair<int, int>>> seenEdges;

    auto alreadyHave = [&](SceneMesh* owner, int a, int b) {
        const auto key = canonicalPair(a, b);
        for (const auto& s : seenEdges)
        {
            if (s.first == owner && s.second == key)
                return true;
        }
        return false;
    };

    auto markSeen = [&](SceneMesh* owner, int a, int b) {
        seenEdges.emplace_back(owner, canonicalPair(a, b));
    };

    for (const TriHit& th : triHits)
    {
        if (th.geomId >= m_meshes.size())
            continue;

        const MeshAccel& accel = m_meshes[th.geomId];
        if (!accel.owner)
            continue;

        if (th.primId >= accel.triToVerts.size() ||
            th.primId >= accel.triToPoly.size())
            continue;

        const SysMesh* sys = accel.owner->sysMesh();
        if (!sys)
            continue;

        const auto& triVerts  = accel.triToVerts[th.primId];
        const int   polyIndex = accel.triToPoly[th.primId];

        const int v0 = triVerts[0];
        const int v1 = triVerts[1];
        const int v2 = triVerts[2];

        const int edgePairs[3][2] = {
            {v0, v1},
            {v1, v2},
            {v2, v0},
        };

        for (int e = 0; e < 3; ++e)
        {
            const int aIdx = edgePairs[e][0];
            const int bIdx = edgePairs[e][1];

            // Skip non-boundary edges (triangulation diagonals)
            if (!isPolygonBoundaryEdge(sys, polyIndex, aIdx, bIdx))
                continue;

            if (!sys->vert_valid(aIdx) || !sys->vert_valid(bIdx))
                continue;

            if (alreadyHave(accel.owner, aIdx, bIdx))
                continue;

            const glm::vec3 a = sys->vert_position(aIdx);
            const glm::vec3 b = sys->vert_position(bIdx);

            RaySegmentHit rs = closestRaySegment(ray, a, b);
            if (!rs.valid || rs.dist2 > kTol2)
                continue;

            MeshHit h;
            h.mesh  = accel.owner;
            h.dist  = rs.tRay;
            h.index = aIdx;
            h.other = bIdx;
            result.push_back(h);

            markSeen(accel.owner, aIdx, bIdx);
        }
    }

    return result;
}

std::vector<MeshHit> SceneQueryEmbree::queryPolys(const Viewport* /*vp*/,
                                                  const Scene* /*scene*/,
                                                  const un::ray& ray) const
{
    std::vector<MeshHit> result;

    if (!m_device || !m_rtcScene)
        return result;

    const auto triHits = intersectAllTriangles(m_rtcScene, ray);
    if (triHits.empty())
        return result;

    auto alreadyHave = [&](SceneMesh* owner, int polyIdx) {
        for (const MeshHit& h : result)
        {
            if (h.mesh == owner && h.index == polyIdx)
                return true;
        }
        return false;
    };

    for (const TriHit& th : triHits)
    {
        if (th.geomId >= m_meshes.size())
            continue;

        const MeshAccel& accel = m_meshes[th.geomId];
        if (!accel.owner)
            continue;

        if (th.primId >= accel.triToPoly.size())
            continue;

        const int polyIndex = accel.triToPoly[th.primId];
        if (alreadyHave(accel.owner, polyIndex))
            continue;

        MeshHit h;
        h.mesh  = accel.owner;
        h.dist  = th.t;
        h.index = polyIndex;
        h.other = -1;
        result.push_back(h);
    }

    return result;
}
