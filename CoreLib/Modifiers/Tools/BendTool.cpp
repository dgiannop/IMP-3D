#include "BendTool.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <glm/gtc/constants.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    static glm::vec3 axisVec(const glm::ivec3& a)
    {
        // AXIS property guarantees one of these is 1 or -1
        return glm::normalize(glm::vec3(a));
    }

    static float safeRadius(float r, float fallback)
    {
        if (std::isfinite(r) && r > 1e-6f)
            return r;
        return std::max(1e-4f, fallback);
    }

    static void makeBasis(const glm::vec3& axis,
                          glm::vec3&       e0,
                          glm::vec3&       e1,
                          glm::vec3&       e2)
    {
        // e0 = bend axis
        e0 = axis;

        // pick a helper not parallel to axis
        const glm::vec3 helper =
            (std::abs(e0.y) < 0.9f) ? glm::vec3{0, 1, 0}
                                    : glm::vec3{1, 0, 0};

        e2 = glm::normalize(glm::cross(e0, helper)); // preserved
        e1 = glm::normalize(glm::cross(e2, e0));     // bend direction
    }

    static glm::vec3 bendLocal(const glm::vec3& local,
                               const glm::vec3& e0,
                               const glm::vec3& e1,
                               const glm::vec3& e2,
                               float            angleRad,
                               float            radius,
                               float            halfLen)
    {
        if (halfLen < 1e-6f)
            return local;

        const float t = glm::dot(local, e0); // along axis
        const float v = glm::dot(local, e1); // bend dir
        const float w = glm::dot(local, e2); // preserved

        // map t → angle
        const float x     = std::clamp(t / halfLen, -1.0f, 1.0f);
        const float theta = x * (0.5f * angleRad);

        const float s = std::sin(theta);
        const float c = std::cos(theta);

        const float R  = radius;
        const float rr = R + v;

        const float t2 = rr * s;
        const float v2 = rr * c - R;

        return e0 * t2 + e1 * v2 + e2 * w;
    }
} // namespace

BendTool::BendTool()
{
    addProperty("Angle", PropertyType::FLOAT, &m_angleDeg);
    addProperty("Radius", PropertyType::FLOAT, &m_radius);
    addProperty("Axis", PropertyType::AXIS, &m_axis);
}

void BendTool::activate(Scene*)
{
}

void BendTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_angleDeg))
        return;

    const glm::vec3 axis = axisVec(m_axis);

    glm::vec3 e0{}, e1{}, e2{};
    makeBasis(axis, e0, e1, e2);

    const glm::vec3 pivot   = sel::selection_center_bounds(scene);
    auto            vertMap = sel::to_verts(scene);

    // project bounds along axis
    float tMin = +FLT_MAX;
    float tMax = -FLT_MAX;
    bool  any  = false;

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 p = mesh->vert_position(vi) - pivot;
            const float     t = glm::dot(p, e0);
            tMin              = std::min(tMin, t);
            tMax              = std::max(tMax, t);
            any               = true;
        }
    }

    if (!any)
        return;

    const float halfLen  = 0.5f * (tMax - tMin);
    const float autoR    = std::max(halfLen, 0.001f);
    const float R        = safeRadius(m_radius, autoR);
    const float angleRad = glm::radians(m_angleDeg);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 p     = mesh->vert_position(vi);
            const glm::vec3 local = p - pivot;

            const glm::vec3 bent = bendLocal(local, e0, e1, e2, angleRad, R, halfLen);
            mesh->move_vert(vi, pivot + bent);
        }
    }
}

void BendTool::mouseDown(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_startAngleDeg = m_angleDeg;
    m_startX        = event.x;
}

void BendTool::mouseDrag(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    const int32_t dx = event.x - m_startX;

    // 200px → 90°
    constexpr float kDegPerPixel = 90.0f / 200.0f;
    m_angleDeg                   = m_startAngleDeg + float(dx) * kDegPerPixel;
}

void BendTool::mouseUp(Viewport*, Scene* scene, const CoreEvent&)
{
    if (scene)
        scene->commitMeshChanges();

    m_angleDeg = 0.0f;
}

void BendTool::render(Viewport*, Scene*)
{
}

OverlayHandler* BendTool::overlayHandler()
{
    return nullptr;
}
