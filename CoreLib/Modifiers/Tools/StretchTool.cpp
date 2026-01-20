#include "StretchTool.hpp"

#include <algorithm>
#include <cmath>

#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    static glm::vec3 clampScale(const glm::vec3& s)
    {
        glm::vec3 out = s;
        out.x         = std::max(0.0001f, out.x);
        out.y         = std::max(0.0001f, out.y);
        out.z         = std::max(0.0001f, out.z);
        return out;
    }
} // namespace

StretchTool::StretchTool()
{
    addProperty("X", PropertyType::FLOAT, &m_scale.x);
    addProperty("Y", PropertyType::FLOAT, &m_scale.y);
    addProperty("Z", PropertyType::FLOAT, &m_scale.z);
}

void StretchTool::activate(Scene*)
{
}

void StretchTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_scale - glm::vec3{1.0f}))
        return;

    const glm::vec3 s = clampScale(m_scale);

    const glm::vec3 pivot   = sel::selection_center_bounds(scene);
    auto            vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        for (int32_t vi : verts)
        {
            const glm::vec3 p = mesh->vert_position(vi);
            const glm::vec3 r = p - pivot;
            mesh->move_vert(vi, pivot + glm::vec3{r.x * s.x, r.y * s.y, r.z * s.z});
        }
    }
}

void StretchTool::mouseDown(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_startScale = m_scale;
    m_pivot      = sel::selection_center_bounds(scene);
    m_startX     = event.x;
    m_startY     = event.y;
}

void StretchTool::mouseDrag(Viewport*, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    const int32_t dx = event.x - m_startX;
    const int32_t dy = event.y - m_startY;

    // Same exponential mapping as uniform scale, per-axis.
    constexpr float k  = 1.0f / 100.0f;
    const float     fx = std::pow(2.0f, float(dx) * k);
    const float     fy = std::pow(2.0f, float(-dy) * k);

    m_scale = m_startScale;
    m_scale.x *= fx;
    m_scale.y *= fy;
    m_scale = clampScale(m_scale);
}

void StretchTool::mouseUp(Viewport*, Scene* scene, const CoreEvent&)
{
    if (scene)
        scene->commitMeshChanges();

    m_scale = glm::vec3{1.0f, 1.0f, 1.0f};
}

void StretchTool::render(Viewport*, Scene*)
{
}

OverlayHandler* StretchTool::overlayHandler()
{
    return nullptr;
}
