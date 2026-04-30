//=============================================================================
// TreeGeneratorTool.cpp
//=============================================================================
#include "TreeGeneratorTool.hpp"

#include <SysMesh.hpp>
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

#include "CoreUtilities.hpp"
#include "SceneMesh.hpp"

namespace
{
    constexpr float kPi = std::numbers::pi_v<float>;

    struct Attractor
    {
        glm::vec3 p{0.0f};
        bool      alive = true;
    };

    struct BranchNode
    {
        glm::vec3 p{0.0f};
        glm::vec3 dir{0.0f, 0.0f, 1.0f};
        float     radius = 0.02f;
        int32_t   parent = -1;
        int32_t   depth  = 0;
    };

    struct RingVert
    {
        int32_t   v = -1;
        glm::vec3 normal{0.0f, 0.0f, 1.0f};
        float     u = 0.0f;
    };

    using Ring = std::vector<RingVert>;

    float randRange(std::mt19937& rng, float a, float b)
    {
        std::uniform_real_distribution<float> d(a, b);
        return d(rng);
    }

    glm::vec3 randomUnit(std::mt19937& rng)
    {
        const float z = randRange(rng, -1.0f, 1.0f);
        const float a = randRange(rng, 0.0f, 2.0f * kPi);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));

        return glm::vec3(std::cos(a) * r, std::sin(a) * r, z);
    }

    glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback)
    {
        const float len2 = glm::dot(v, v);
        if (len2 <= 1.0e-10f)
            return fallback;

        return v / std::sqrt(len2);
    }

    glm::vec3 orientFromZUp(const glm::vec3& p, const glm::ivec3& axis)
    {
        if (axis == glm::ivec3(0, 0, 1))
            return p;

        if (axis == glm::ivec3(0, 1, 0))
            return glm::vec3(p.x, p.z, p.y);

        if (axis == glm::ivec3(1, 0, 0))
            return glm::vec3(p.z, p.y, p.x);

        if (axis == glm::ivec3(0, -1, 0))
            return glm::vec3(p.x, -p.z, p.y);

        if (axis == glm::ivec3(0, 0, -1))
            return glm::vec3(p.x, p.y, -p.z);

        if (axis == glm::ivec3(-1, 0, 0))
            return glm::vec3(-p.z, p.y, p.x);

        return glm::vec3(p.x, p.z, p.y);
    }

    glm::vec3 toWorldPos(
        const glm::vec3&  local,
        const glm::vec3&  center,
        const glm::ivec3& axis)
    {
        return center + orientFromZUp(local, axis);
    }

    glm::vec3 toWorldDir(
        const glm::vec3&  localDir,
        const glm::ivec3& axis)
    {
        return safeNormalize(orientFromZUp(localDir, axis), glm::vec3(0, 1, 0));
    }

    void makeFrame(const glm::vec3& tangent, glm::vec3& right, glm::vec3& up)
    {
        const glm::vec3 t = safeNormalize(tangent, glm::vec3(0, 0, 1));

        const glm::vec3 helper =
            std::abs(t.z) > 0.85f
                ? glm::vec3(1, 0, 0)
                : glm::vec3(0, 0, 1);

        right = safeNormalize(glm::cross(helper, t), glm::vec3(1, 0, 0));
        up    = safeNormalize(glm::cross(t, right), glm::vec3(0, 1, 0));
    }

    int ensureNormalMap(SysMesh* mesh)
    {
        const int m = mesh->map_find(0);
        return (m >= 0) ? m : mesh->map_create(0, 0, 3);
    }

    int ensureUvMap(SysMesh* mesh)
    {
        const int m = mesh->map_find(1);
        return (m >= 0) ? m : mesh->map_create(1, 0, 2);
    }

    Ring createRing(
        SysMesh*          mesh,
        const glm::vec3&  localCenter,
        const glm::vec3&  tangent,
        float             radius,
        int32_t           sides,
        const glm::vec3&  worldCenter,
        const glm::ivec3& axis)
    {
        Ring ring;
        ring.reserve(static_cast<size_t>(sides));

        glm::vec3 right;
        glm::vec3 up;
        makeFrame(tangent, right, up);

        for (int32_t i = 0; i < sides; ++i)
        {
            const float u = float(i) / float(sides);
            const float a = u * 2.0f * kPi;

            const glm::vec3 radial =
                std::cos(a) * right +
                std::sin(a) * up;

            const glm::vec3 localP = localCenter + radial * radius;
            const glm::vec3 worldP = toWorldPos(localP, worldCenter, axis);
            const glm::vec3 worldN = toWorldDir(radial, axis);

            RingVert rv;
            rv.v      = mesh->create_vert(worldP);
            rv.normal = worldN;
            rv.u      = u;

            ring.push_back(rv);
        }

        return ring;
    }

    void emitQuadMapped(
        SysMesh*  mesh,
        int       normMap,
        int       uvMap,
        int32_t   a,
        int32_t   b,
        int32_t   c,
        int32_t   d,
        glm::vec3 nA,
        glm::vec3 nB,
        glm::vec3 nC,
        glm::vec3 nD,
        glm::vec2 uvA,
        glm::vec2 uvB,
        glm::vec2 uvC,
        glm::vec2 uvD,
        uint32_t  material)
    {
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return;

        if (a == b || a == c || a == d || b == c || b == d || c == d)
            return;

        const glm::vec3 pa = mesh->vert_position(a);
        const glm::vec3 pb = mesh->vert_position(b);
        const glm::vec3 pd = mesh->vert_position(d);

        const glm::vec3 expectedN =
            safeNormalize(nA + nB + nC + nD, glm::vec3(0, 1, 0));

        const glm::vec3 geomN =
            safeNormalize(glm::cross(pb - pa, pd - pa), expectedN);

        if (glm::dot(geomN, expectedN) < 0.0f)
        {
            std::swap(b, d);
            std::swap(nB, nD);
            std::swap(uvB, uvD);
        }

        SysPolyVerts pv{a, b, c, d};

        const int32_t pid = mesh->create_poly(pv, material);

        SysPolyVerts nr{
            mesh->map_create_vert(normMap, glm::value_ptr(nA)),
            mesh->map_create_vert(normMap, glm::value_ptr(nB)),
            mesh->map_create_vert(normMap, glm::value_ptr(nC)),
            mesh->map_create_vert(normMap, glm::value_ptr(nD)),
        };

        SysPolyVerts uv{
            mesh->map_create_vert(uvMap, &uvA.x),
            mesh->map_create_vert(uvMap, &uvB.x),
            mesh->map_create_vert(uvMap, &uvC.x),
            mesh->map_create_vert(uvMap, &uvD.x),
        };

        mesh->map_create_poly(normMap, pid, nr);
        mesh->map_create_poly(uvMap, pid, uv);
    }

    void connectRingsMapped(
        SysMesh*    mesh,
        int         normMap,
        int         uvMap,
        const Ring& a,
        const Ring& b,
        float       v0,
        float       v1,
        uint32_t    mat)
    {
        const int32_t n = static_cast<int32_t>(std::min(a.size(), b.size()));

        for (int32_t i = 0; i < n; ++i)
        {
            const int32_t j = (i + 1) % n;

            const float u0 = a[i].u;
            const float u1 = (j == 0) ? 1.0f : a[j].u;

            emitQuadMapped(
                mesh,
                normMap,
                uvMap,
                a[i].v,
                a[j].v,
                b[j].v,
                b[i].v,
                a[i].normal,
                a[j].normal,
                b[j].normal,
                b[i].normal,
                glm::vec2(u0, v0),
                glm::vec2(u1, v0),
                glm::vec2(u1, v1),
                glm::vec2(u0, v1),
                mat);
        }
    }

    void createLeafCard(
        SysMesh*          mesh,
        int               normMap,
        int               uvMap,
        const glm::vec3&  localCenter,
        const glm::vec3&  direction,
        float             size,
        uint32_t          mat,
        const glm::vec3&  worldCenter,
        const glm::ivec3& axis)
    {
        const glm::vec3 f = safeNormalize(direction, glm::vec3(1, 0, 0));

        glm::vec3 right;
        glm::vec3 up;
        makeFrame(f, right, up);

        const float w = size * 0.45f;
        const float h = size;

        const glm::vec3 p0L = localCenter - right * w;
        const glm::vec3 p1L = localCenter + right * w;
        const glm::vec3 p2L = localCenter + right * w + up * h;
        const glm::vec3 p3L = localCenter - right * w + up * h;

        const glm::vec3 p0 = toWorldPos(p0L, worldCenter, axis);
        const glm::vec3 p1 = toWorldPos(p1L, worldCenter, axis);
        const glm::vec3 p2 = toWorldPos(p2L, worldCenter, axis);
        const glm::vec3 p3 = toWorldPos(p3L, worldCenter, axis);

        const glm::vec3 n =
            safeNormalize(glm::cross(p1 - p0, p3 - p0), toWorldDir(f, axis));

        const int32_t v0 = mesh->create_vert(p0);
        const int32_t v1 = mesh->create_vert(p1);
        const int32_t v2 = mesh->create_vert(p2);
        const int32_t v3 = mesh->create_vert(p3);

        emitQuadMapped(
            mesh,
            normMap,
            uvMap,
            v0,
            v1,
            v2,
            v3,
            n,
            n,
            n,
            n,
            glm::vec2(0, 0),
            glm::vec2(1, 0),
            glm::vec2(1, 1),
            glm::vec2(0, 1),
            mat);
    }

    TreeGeneratorTool::Settings applyLod(TreeGeneratorTool::Settings s)
    {
        if (s.lod == 1)
        {
            s.attractionPoints = std::min(s.attractionPoints, 260);
            s.maxIterations    = std::min(s.maxIterations, 100);
            s.tubeSides        = std::min(s.tubeSides, 6);
            s.leafCards        = std::min(s.leafCards, 180);
        }
        else if (s.lod >= 2)
        {
            s.attractionPoints = std::min(s.attractionPoints, 120);
            s.maxIterations    = std::min(s.maxIterations, 60);
            s.tubeSides        = std::min(s.tubeSides, 4);
            s.leafCards        = std::min(s.leafCards, 60);
        }

        return s;
    }

    std::vector<Attractor> createAttractors(
        const TreeGeneratorTool::Settings& s,
        std::mt19937&                      rng)
    {
        std::vector<Attractor> pts;
        pts.reserve(static_cast<size_t>(s.attractionPoints));

        const float crownBase   = s.height * s.trunkHeightRatio;
        const float crownHeight = s.height - crownBase;

        int32_t guard = 0;

        while (static_cast<int32_t>(pts.size()) < s.attractionPoints &&
               guard < s.attractionPoints * 30)
        {
            ++guard;

            const glm::vec3 u = randomUnit(rng);
            const float     r = std::cbrt(randRange(rng, 0.0f, 1.0f));

            glm::vec3 p;
            p.x = u.x * r * s.crownRadius;
            p.y = u.y * r * s.crownRadius;
            p.z = crownBase + (u.z * 0.5f + 0.5f) * crownHeight;

            const float ht =
                std::clamp((p.z - crownBase) / std::max(0.001f, crownHeight), 0.0f, 1.0f);

            const float shape   = std::sin(ht * kPi);
            const float allowed = s.crownRadius * (0.25f + 0.75f * shape);

            if (std::sqrt(p.x * p.x + p.y * p.y) <= allowed)
                pts.push_back({p, true});
        }

        return pts;
    }

    std::vector<BranchNode> buildSkeleton(
        const TreeGeneratorTool::Settings& s,
        std::mt19937&                      rng)
    {
        std::vector<Attractor> attractors = createAttractors(s, rng);

        std::vector<BranchNode> nodes;
        nodes.reserve(static_cast<size_t>(s.maxIterations * 8));

        nodes.push_back({glm::vec3(0, 0, 0),
                         glm::vec3(0, 0, 1),
                         s.trunkRadius,
                         -1,
                         0});

        int32_t     parent   = 0;
        const float trunkTop = s.height * s.trunkHeightRatio;

        while (nodes[parent].p.z < trunkTop)
        {
            BranchNode n;
            n.p      = nodes[parent].p + glm::vec3(0, 0, s.growStep);
            n.dir    = glm::vec3(0, 0, 1);
            n.parent = parent;
            n.depth  = nodes[parent].depth + 1;

            nodes.push_back(n);
            parent = static_cast<int32_t>(nodes.size()) - 1;
        }

        for (int32_t iter = 0; iter < s.maxIterations; ++iter)
        {
            std::vector<glm::vec3> accum(nodes.size(), glm::vec3(0.0f));
            std::vector<int32_t>   counts(nodes.size(), 0);

            int32_t aliveCount = 0;

            for (Attractor& a : attractors)
            {
                if (!a.alive)
                    continue;

                ++aliveCount;

                float   bestD2   = std::numeric_limits<float>::max();
                int32_t bestNode = -1;

                for (int32_t i = 0; i < static_cast<int32_t>(nodes.size()); ++i)
                {
                    const glm::vec3 d  = a.p - nodes[i].p;
                    const float     d2 = glm::dot(d, d);

                    if (d2 < s.killDistance * s.killDistance)
                    {
                        a.alive  = false;
                        bestNode = -1;
                        break;
                    }

                    if (d2 < s.influenceDistance * s.influenceDistance && d2 < bestD2)
                    {
                        bestD2   = d2;
                        bestNode = i;
                    }
                }

                if (bestNode >= 0)
                {
                    accum[bestNode] += safeNormalize(a.p - nodes[bestNode].p, nodes[bestNode].dir);
                    counts[bestNode]++;
                }
            }

            if (aliveCount == 0)
                break;

            const int32_t startCount = static_cast<int32_t>(nodes.size());

            for (int32_t i = 0; i < startCount; ++i)
            {
                if (counts[i] == 0)
                    continue;

                glm::vec3 dir = accum[i] / float(counts[i]);
                dir += glm::vec3(0, 0, s.upwardBias);
                dir += randomUnit(rng) * s.randomness;
                dir = safeNormalize(dir, nodes[i].dir);

                BranchNode n;
                n.p      = nodes[i].p + dir * s.growStep;
                n.dir    = dir;
                n.parent = i;
                n.depth  = nodes[i].depth + 1;

                nodes.push_back(n);

                if (nodes.size() > 20000)
                    break;
            }

            if (nodes.size() > 20000)
                break;
        }

        return nodes;
    }

    void solveRadii(std::vector<BranchNode>& nodes, float trunkRadius)
    {
        std::vector<std::vector<int32_t>> children(nodes.size());

        for (int32_t i = 1; i < static_cast<int32_t>(nodes.size()); ++i)
        {
            const int32_t p = nodes[i].parent;
            if (p >= 0 && p < static_cast<int32_t>(nodes.size()))
                children[p].push_back(i);
        }

        std::vector<float> area(nodes.size(), 0.0f);

        for (int32_t i = static_cast<int32_t>(nodes.size()) - 1; i >= 0; --i)
        {
            if (children[i].empty())
            {
                area[i] = 1.0f;
            }
            else
            {
                float sum = 0.0f;
                for (int32_t c : children[i])
                    sum += area[c];

                area[i] = std::max(1.0f, sum);
            }
        }

        const float rootArea = std::max(1.0f, area[0]);

        for (int32_t i = 0; i < static_cast<int32_t>(nodes.size()); ++i)
        {
            const float t   = std::sqrt(area[i] / rootArea);
            nodes[i].radius = std::max(0.012f, trunkRadius * t);
        }
    }

    void meshSkeleton(
        SysMesh*                           mesh,
        int                                normMap,
        int                                uvMap,
        const std::vector<BranchNode>&     nodes,
        const TreeGeneratorTool::Settings& s,
        const glm::vec3&                   center,
        const glm::ivec3&                  axis)
    {
        if (!mesh || nodes.size() < 2)
            return;

        for (int32_t i = 1; i < static_cast<int32_t>(nodes.size()); ++i)
        {
            const int32_t pidx = nodes[i].parent;

            if (pidx < 0 || pidx >= static_cast<int32_t>(nodes.size()))
                continue;

            const BranchNode& parent = nodes[pidx];
            const BranchNode& child  = nodes[i];

            const glm::vec3 tangent = child.p - parent.p;
            const float     len2    = glm::dot(tangent, tangent);

            if (len2 <= 1.0e-8f)
                continue;

            const float v0 = parent.p.z / std::max(0.001f, s.height);
            const float v1 = child.p.z / std::max(0.001f, s.height);

            const Ring a = createRing(
                mesh,
                parent.p,
                tangent,
                parent.radius,
                s.tubeSides,
                center,
                axis);

            const Ring b = createRing(
                mesh,
                child.p,
                tangent,
                child.radius,
                s.tubeSides,
                center,
                axis);

            connectRingsMapped(
                mesh,
                normMap,
                uvMap,
                a,
                b,
                v0,
                v1,
                s.barkMaterial);
        }
    }

    void addLeaves(
        SysMesh*                           mesh,
        int                                normMap,
        int                                uvMap,
        const std::vector<BranchNode>&     nodes,
        const TreeGeneratorTool::Settings& s,
        std::mt19937&                      rng,
        const glm::vec3&                   center,
        const glm::ivec3&                  axis)
    {
        if (!s.createLeaves || nodes.empty())
            return;

        std::vector<std::vector<int32_t>> children(nodes.size());

        for (int32_t i = 1; i < static_cast<int32_t>(nodes.size()); ++i)
        {
            const int32_t p = nodes[i].parent;
            if (p >= 0 && p < static_cast<int32_t>(nodes.size()))
                children[p].push_back(i);
        }

        std::vector<int32_t> tips;

        for (int32_t i = 0; i < static_cast<int32_t>(nodes.size()); ++i)
        {
            if (children[i].empty() && nodes[i].p.z > s.height * s.trunkHeightRatio)
                tips.push_back(i);
        }

        if (tips.empty())
            return;

        for (int32_t i = 0; i < s.leafCards; ++i)
        {
            const int32_t     tip = tips[static_cast<size_t>(i) % tips.size()];
            const BranchNode& n   = nodes[tip];

            glm::vec3 dir = n.dir + randomUnit(rng) * 0.45f + glm::vec3(0, 0, 0.25f);
            dir           = safeNormalize(dir, glm::vec3(0, 0, 1));

            const glm::vec3 p =
                n.p + randomUnit(rng) * randRange(rng, 0.02f, 0.18f);

            createLeafCard(
                mesh,
                normMap,
                uvMap,
                p,
                dir,
                s.leafSize * randRange(rng, 0.75f, 1.35f),
                s.leafMaterial,
                center,
                axis);
        }
    }
} // namespace

TreeGeneratorTool::TreeGeneratorTool()
{
    addProperty("Seed", PropertyType::INT, &m_settings.seed);
    addProperty("LOD", PropertyType::INT, &m_settings.lod, 0, 2);

    addProperty("Center X", PropertyType::FLOAT, &m_center.x);
    addProperty("Center Y", PropertyType::FLOAT, &m_center.y);
    addProperty("Center Z", PropertyType::FLOAT, &m_center.z);
    addProperty("Axis", PropertyType::AXIS, &m_axis);

    addProperty("Height", PropertyType::FLOAT, &m_settings.height);
    addProperty("Crown Radius", PropertyType::FLOAT, &m_settings.crownRadius);
    addProperty("Trunk Radius", PropertyType::FLOAT, &m_settings.trunkRadius);

    addProperty("Attraction Points", PropertyType::INT, &m_settings.attractionPoints, 20, 3000);
    addProperty("Max Iterations", PropertyType::INT, &m_settings.maxIterations, 10, 600);

    addProperty("Kill Distance", PropertyType::FLOAT, &m_settings.killDistance);
    addProperty("Influence Distance", PropertyType::FLOAT, &m_settings.influenceDistance);
    addProperty("Grow Step", PropertyType::FLOAT, &m_settings.growStep);

    addProperty("Trunk Height Ratio", PropertyType::FLOAT, &m_settings.trunkHeightRatio);
    addProperty("Upward Bias", PropertyType::FLOAT, &m_settings.upwardBias);
    addProperty("Randomness", PropertyType::FLOAT, &m_settings.randomness);

    addProperty("Tube Sides", PropertyType::INT, &m_settings.tubeSides, 4, 24);

    addProperty("Create Leaves", PropertyType::BOOL, &m_settings.createLeaves);
    addProperty("Leaf Cards", PropertyType::INT, &m_settings.leafCards, 0, 5000);
    addProperty("Leaf Size", PropertyType::FLOAT, &m_settings.leafSize);
}

void TreeGeneratorTool::activate(Scene* scene)
{
    propertiesChanged(scene);
}

void TreeGeneratorTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (!m_sceneMesh)
        m_sceneMesh = scene->createSceneMesh("Tree");

    SysMesh* mesh = m_sceneMesh->sysMesh();
    if (!mesh)
        return;

    TreeGeneratorTool::generateTree(mesh, m_settings, m_center, m_axis);
}

void TreeGeneratorTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDown(vp, scene, event);
}

void TreeGeneratorTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseDrag(vp, scene, event);
}

void TreeGeneratorTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    m_gizmo.mouseUp(vp, scene, event);
    propertiesChanged(scene);
}

void TreeGeneratorTool::render(Viewport* vp, Scene* scene)
{
    m_gizmo.render(vp, scene);
}

OverlayHandler* TreeGeneratorTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}

void TreeGeneratorTool::generateTree(
    SysMesh*          mesh,
    const Settings&   input,
    const glm::vec3&  center,
    const glm::ivec3& axis)
{
    if (!mesh)
        return;

    Settings s = applyLod(input);

    s.height            = std::max(0.1f, s.height);
    s.crownRadius       = std::max(0.05f, s.crownRadius);
    s.trunkRadius       = std::max(0.01f, s.trunkRadius);
    s.attractionPoints  = std::max(10, s.attractionPoints);
    s.maxIterations     = std::max(1, s.maxIterations);
    s.killDistance      = std::max(0.01f, s.killDistance);
    s.influenceDistance = std::max(s.killDistance * 2.0f, s.influenceDistance);
    s.growStep          = std::max(0.01f, s.growStep);
    s.trunkHeightRatio  = std::clamp(s.trunkHeightRatio, 0.05f, 0.85f);
    s.tubeSides         = std::max(4, s.tubeSides);

    const int normMap = ensureNormalMap(mesh);
    const int uvMap   = ensureUvMap(mesh);

    std::mt19937 rng(static_cast<uint32_t>(s.seed));

    std::vector<BranchNode> nodes = buildSkeleton(s, rng);
    solveRadii(nodes, s.trunkRadius);

    meshSkeleton(mesh, normMap, uvMap, nodes, s, center, axis);
    addLeaves(mesh, normMap, uvMap, nodes, s, rng, center, axis);
}
