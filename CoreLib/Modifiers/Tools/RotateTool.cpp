#include "RotateTool.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <unordered_set>

#include "Scene.hpp"
#include "Viewport.hpp"

namespace
{
    /**
     * @brief Converts XYZ Euler angles (degrees) into a normalized quaternion.
     *
     * Rotation order is X (Pitch), then Y (Yaw), then Z (Roll).
     * The resulting quaternion represents a delta rotation in world space.
     */
    static glm::quat eulerDegToQuatXYZ(const glm::vec3& deg)
    {
        const glm::vec3 rad = glm::radians(deg);

        const glm::quat qx = glm::angleAxis(rad.x, glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::quat qy = glm::angleAxis(rad.y, glm::vec3{0.0f, 1.0f, 0.0f});
        const glm::quat qz = glm::angleAxis(rad.z, glm::vec3{0.0f, 0.0f, 1.0f});

        return glm::normalize(qz * qy * qx);
    }

    /// Normal map id used by the application (face-varying, dim = 3).
    constexpr int kNormMapId = 0;

} // namespace

RotateTool::RotateTool()
{
    addProperty("Pitch", PropertyType::FLOAT, &m_anglesDeg.x);
    addProperty("Yaw", PropertyType::FLOAT, &m_anglesDeg.y);
    addProperty("Roll", PropertyType::FLOAT, &m_anglesDeg.z);

    // Rotation is applied as a delta; keep angles zeroed by default.
    m_anglesDeg = glm::vec3{0.0f};
}

void RotateTool::activate(Scene*)
{
    // No activation logic required.
}

void RotateTool::propertiesChanged(Scene* scene)
{
    if (!scene)
        return;

    scene->abortMeshChanges();

    if (un::is_zero(m_anglesDeg))
        return;

    const glm::quat q     = eulerDegToQuatXYZ(m_anglesDeg);
    const glm::vec3 pivot = sel::selection_center_bounds(scene);

    auto vertMap = sel::to_verts(scene);

    for (auto& [mesh, verts] : vertMap)
    {
        if (!mesh || verts.empty())
            continue;

        // ------------------------------------------------------------
        // Rotate vertex positions around the selection pivot
        // ------------------------------------------------------------
        for (int32_t vi : verts)
        {
            if (!mesh->vert_valid(vi))
                continue;

            const glm::vec3 p  = mesh->vert_position(vi);
            const glm::vec3 r  = p - pivot;
            const glm::vec3 rp = q * r;

            mesh->move_vert(vi, pivot + rp);
        }

        // ------------------------------------------------------------
        // Rebuild face-varying normals for affected polygons
        //
        // Normals in SysMesh are stored per face corner.
        // Instead of attempting to rotate existing normal map data,
        // face normals are recomputed from the updated geometry and
        // written back as new face-varying normals.
        // ------------------------------------------------------------

        int32_t normMap = mesh->map_find(kNormMapId);
        if (normMap == -1)
            normMap = mesh->map_create(kNormMapId, /*type*/ 0, /*dim*/ 3);

        if (normMap < 0 || mesh->map_dim(normMap) != 3)
            continue;

        // Collect all polygons incident to the rotated vertices.
        std::unordered_set<int32_t> touchedPolys;
        touchedPolys.reserve(verts.size() * 4ull);

        for (int32_t vi : verts)
        {
            if (!mesh->vert_valid(vi))
                continue;

            const SysVertPolys& incident = mesh->vert_polys(vi);
            for (int32_t pid : incident)
                if (mesh->poly_valid(pid))
                    touchedPolys.insert(pid);
        }

        // Recompute and write flat normals per polygon corner.
        for (int32_t pid : touchedPolys)
        {
            if (!mesh->poly_valid(pid))
                continue;

            const SysPolyVerts& pv = mesh->poly_verts(pid);
            if (pv.size() < 3)
                continue;

            glm::vec3 fn = un::safe_normalize(mesh->poly_normal(pid),
                                              glm::vec3{0.0f, 1.0f, 0.0f});

            SysPolyVerts nPoly;
            nPoly.reserve(pv.size());

            bool ok = true;
            for (size_t i = 0; i < pv.size(); ++i)
            {
                const float   tmp[3] = {fn.x, fn.y, fn.z};
                const int32_t mv     = mesh->map_create_vert(normMap, tmp);

                if (mv < 0)
                {
                    ok = false;
                    break;
                }

                nPoly.push_back(mv);
            }

            if (!ok || nPoly.size() != pv.size())
                continue;

            if (mesh->map_poly_valid(normMap, pid))
                mesh->map_remove_poly(normMap, pid);

            mesh->map_create_poly(normMap, pid, nPoly);
        }
    }
}

void RotateTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    /**
     * Rotation angles represent a delta from the current scene state.
     * Resetting angles at mouse down ensures stable preview behavior.
     */
    m_anglesDeg = glm::vec3{0.0f};

    m_gizmo.mouseDown(vp, scene, event);

    propertiesChanged(scene);
}

void RotateTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!vp || !scene)
        return;

    m_gizmo.mouseDrag(vp, scene, event);
}

void RotateTool::mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event)
{
    if (!scene)
        return;

    m_gizmo.mouseUp(vp, scene, event);

    // Commit previewed geometry into the mesh.
    scene->commitMeshChanges();

    // Reset delta for the next interaction.
    m_anglesDeg = glm::vec3{0.0f};
}

void RotateTool::render(Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    m_gizmo.render(vp, scene);
}

OverlayHandler* RotateTool::overlayHandler()
{
    return &m_gizmo.overlayHandler();
}
