#include "CmdFreeze.hpp"

#include <algorithm>
#include <glm/gtx/norm.hpp>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SubdivEvaluator.hpp"
#include "SysMesh.hpp"

static inline glm::vec3 safe_normalize(glm::vec3 v) noexcept
{
    const float l2 = glm::length2(v);
    return (l2 > 1e-20f) ? v * glm::inversesqrt(l2) : glm::vec3(0.0f, 1.0f, 0.0f);
}

bool CmdFreeze::execute(Scene* scene)
{
    if (!scene)
        return false;

    constexpr int UV_OSD_CH = 0; // OSD fvar channel
    constexpr int NORM_ID   = 0; // SysMesh normal map ID
    constexpr int UV_ID     = 1; // SysMesh UV map ID

    bool any = false;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm->selected())
            continue;

        SysMesh*         mesh   = sm->sysMesh();
        SubdivEvaluator* subdiv = sm->subdiv();
        if (!mesh || !subdiv)
            continue;

        // Bake at the currently active subdivision level for this SceneMesh.
        const int lvlReq = std::clamp(sm->subdivisionLevel(), 0, 4);

        // Preserve a reasonable default material BEFORE clearing the mesh.
        // (if the mesh was single-material, this restores the textured material.)
        uint32_t defaultMat = 0;
        if (mesh->num_polys() > 0)
        {
            const auto& polys = mesh->all_polys();
            for (int32_t p : polys)
            {
                if (mesh->poly_valid(p))
                {
                    defaultMat = mesh->poly_material(p);
                    break;
                }
            }
        }

        // Build/refine evaluator self-contained.
        subdiv->onTopologyChanged(mesh, lvlReq);

        OpenSubdiv::Far::TopologyRefiner* ref = subdiv->refiner();
        if (!ref)
            continue;

        const int   lvl = std::clamp(lvlReq, 0, ref->GetMaxLevel());
        const auto& L   = ref->GetLevel(lvl);

        const auto P = subdiv->vertices(); // level-local positions (dense 0..)
        const auto N = subdiv->normals();  // level-local normals   (dense 0..)

        const int faceCnt = L.GetNumFaces();
        if (P.empty() || faceCnt <= 0)
            continue;

        if ((int)P.size() != L.GetNumVertices())
            continue;

        // UV pool for this level (level-local fvar indexing)
        const std::vector<glm::vec2>& UV = subdiv->uvs();

        // Clear mesh completely
        mesh->clear();

        // Ensure maps exist
        int normMap = mesh->map_find(NORM_ID);
        if (normMap == -1)
            normMap = mesh->map_create(NORM_ID, /*type*/ 0, /*dim*/ 3);

        int uvMap = mesh->map_find(UV_ID);
        if (uvMap == -1)
            uvMap = mesh->map_create(UV_ID, /*type*/ 0, /*dim*/ 2);

        const bool wantNormals = (normMap >= 0 && mesh->map_dim(normMap) == 3 && N.size() == P.size());
        const bool wantUVs     = (uvMap >= 0 && L.GetNumFVarChannels() > 0);

        const bool haveUVPool =
            wantUVs &&
            ((int)UV.size() == L.GetNumFVarValues(UV_OSD_CH)); // Must match level-local fvar count

        // Create refined verts (dense)
        std::vector<int32_t> vmap;
        vmap.resize(P.size(), -1);
        for (int i = 0; i < (int)P.size(); ++i)
            vmap[(size_t)i] = mesh->create_vert(P[(size_t)i]);

        // Create polys from OSD faces
        std::vector<int32_t> pidOfFace;
        pidOfFace.assign((size_t)faceCnt, -1);

        for (int f = 0; f < faceCnt; ++f)
        {
            const auto fv = L.GetFaceVertices(f);
            const int  n  = (int)fv.size();
            if (n < 3)
                continue;

            SysPolyVerts poly = {};
            bool         ok   = true;

            for (int viDense : fv)
            {
                if (viDense < 0 || viDense >= (int)vmap.size())
                {
                    ok = false;
                    break;
                }
                poly.push_back(vmap[(size_t)viDense]);
            }

            if (!ok || poly.size() < 3)
                continue;

            // -------------------------------------------------------------
            // Use per-face material from SubdivEvaluator (OSD face-uniform)
            // Fallback to defaultMat if unavailable.
            // -------------------------------------------------------------
            uint32_t mat = defaultMat;
            {
                const uint32_t m = subdiv->faceMaterialId(f);
                if (m != 0u || defaultMat == 0u)
                    mat = m;
            }

            const int32_t pid    = mesh->create_poly(poly, mat);
            pidOfFace[(size_t)f] = pid;
        }

        // Attach UVs per-corner (face-varying)
        if (haveUVPool)
        {
            for (int f = 0; f < faceCnt; ++f)
            {
                const int32_t pid = pidOfFace[(size_t)f];
                if (pid < 0)
                    continue;

                const auto fv  = L.GetFaceVertices(f);
                const auto fuv = L.GetFaceFVarValues(f, UV_OSD_CH);

                if ((int)fuv.size() != (int)fv.size())
                    continue;

                SysPolyVerts uvPoly = {};
                uvPoly.reserve(fv.size());

                bool ok = true;
                for (int idx : fuv)
                {
                    if (idx < 0 || idx >= (int)UV.size())
                    {
                        ok = false;
                        break;
                    }

                    const glm::vec2 u      = UV[(size_t)idx];
                    const float     tmp[2] = {u.x, u.y};

                    // Face-varying, create a fresh map vert for each corner.
                    const int32_t mv = mesh->map_create_vert(uvMap, tmp);
                    if (mv < 0)
                    {
                        ok = false;
                        break;
                    }
                    uvPoly.push_back(mv);
                }

                if (ok && uvPoly.size() == fv.size())
                    mesh->map_create_poly(uvMap, pid, uvPoly);
            }
        }

        // Attach normals per-corner (face-varying)
        if (wantNormals)
        {
            for (int f = 0; f < faceCnt; ++f)
            {
                const int32_t pid = pidOfFace[(size_t)f];
                if (pid < 0)
                    continue;

                const auto fv = L.GetFaceVertices(f);
                if (fv.size() < 3)
                    continue;

                SysPolyVerts nPoly = {};
                nPoly.reserve(fv.size());

                bool ok = true;
                for (int viDense : fv)
                {
                    if (viDense < 0 || viDense >= (int)N.size())
                    {
                        ok = false;
                        break;
                    }

                    const glm::vec3 nn     = safe_normalize(N[(size_t)viDense]);
                    const float     tmp[3] = {nn.x, nn.y, nn.z};

                    // FACE-VARYING, NOT SHARED: fresh normal map vert per corner.
                    const int32_t mv = mesh->map_create_vert(normMap, tmp);
                    if (mv < 0)
                    {
                        ok = false;
                        break;
                    }
                    nPoly.push_back(mv);
                }

                if (ok && nPoly.size() == fv.size())
                    mesh->map_create_poly(normMap, pid, nPoly);
            }
        }

        // Clear selection after topology rewrite
        mesh->clear_selected_verts();
        mesh->clear_selected_edges();
        mesh->clear_selected_polys();

        // Disable subdivision on this mesh (delta API)
        sm->subdivisionLevel(-sm->subdivisionLevel());

        any = true;
    }

    return any;
}
