// HeMeshBridge.cpp
#include "HeMeshBridge.hpp"

#include <algorithm>
#include <cstdint>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SysMesh.hpp"

namespace
{
    static bool near_vec3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-6f) noexcept
    {
        return glm::all(glm::epsilonEqual(a, b, eps));
    }

    static bool valid_poly(const SysMesh* sys, int32_t p) noexcept
    {
        return sys && p >= 0 && sys->poly_valid(p);
    }

    static bool valid_vert(const SysMesh* sys, int32_t v) noexcept
    {
        return sys && v >= 0 && sys->vert_valid(v);
    }

    static std::vector<int32_t> selected_polys_copy(const SysMesh* sys)
    {
        if (!sys)
            return {};

        const auto& sel = sys->selected_polys();
        return std::vector<int32_t>(sel.begin(), sel.end());
    }

    static void sanitize_poly_list(const SysMesh* sys, std::vector<int32_t>& polys)
    {
        polys.erase(std::remove_if(polys.begin(),
                                   polys.end(),
                                   [&](int32_t p) { return !valid_poly(sys, p); }),
                    polys.end());

        std::sort(polys.begin(), polys.end());
        polys.erase(std::unique(polys.begin(), polys.end()), polys.end());
    }

    /**
     * Region:
     *  - always includes editable polys
     *  - optionally includes neighbor polys across EDITABLE boundary edges
     *
     * Editable boundary edge:
     *  - an undirected edge incident to exactly ONE editable poly
     *  - the other incident poly becomes a support poly
     */
    static std::vector<int32_t> compute_region_polys(const SysMesh*             sys,
                                                     std::span<const int32_t>   editable,
                                                     const HeExtractionOptions& opt)
    {
        std::unordered_set<int32_t> editableSet;
        editableSet.reserve(editable.size());

        for (int32_t p : editable)
        {
            if (valid_poly(sys, p))
                editableSet.insert(p);
        }

        std::unordered_set<int32_t> regionSet = editableSet;

        if (!opt.includeBoundaryNeighbors)
        {
            std::vector<int32_t> out(regionSet.begin(), regionSet.end());
            std::sort(out.begin(), out.end());
            return out;
        }

        for (int32_t p : editable)
        {
            if (!valid_poly(sys, p))
                continue;

            for (IndexPair e : sys->poly_edges(p)) // directed OK; edge_polys uses poly_has_edge
            {
                const SysEdgePolys incident = sys->edge_polys(e);

                int     editableCount = 0;
                int32_t otherPoly     = -1;

                for (int32_t ep : incident)
                {
                    if (!valid_poly(sys, ep))
                        continue;

                    if (editableSet.contains(ep))
                        ++editableCount;
                    else
                        otherPoly = ep;
                }

                if (editableCount == 1 && otherPoly >= 0)
                    regionSet.insert(otherPoly);
            }
        }

        std::vector<int32_t> out(regionSet.begin(), regionSet.end());
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

} // namespace

// -----------------------------------------------------------------------------
// Extraction
// -----------------------------------------------------------------------------

HeExtractionResult extract_selected_polys_to_hemesh(SysMesh*                   sys,
                                                    const HeExtractionOptions& opt)
{
    std::vector<int32_t> editable = selected_polys_copy(sys);
    return extract_polys_to_hemesh(sys, editable, opt);
}

HeExtractionResult extract_polys_to_hemesh(SysMesh*                   sys,
                                           std::span<const int32_t>   editableSysPolys,
                                           const HeExtractionOptions& opt)
{
    HeExtractionResult out{};

    if (!sys)
        return out;

    // Snapshot editable polys (SANITIZE!)
    out.editableSysPolys.assign(editableSysPolys.begin(), editableSysPolys.end());
    sanitize_poly_list(sys, out.editableSysPolys);

    // Nothing editable => no-op extraction
    if (out.editableSysPolys.empty())
        return out;

    // Compute region (editable + optional boundary neighbors)
    out.regionSysPolys = compute_region_polys(sys,
                                              std::span<const int32_t>(out.editableSysPolys.data(), out.editableSysPolys.size()),
                                              opt);

    // Allocate Sys->He maps sized to stable ranges
    const uint32_t vb = sys->vert_buffer_size();
    const uint32_t pb = sys->poly_buffer_size();

    out.sysVertToHeVert.assign(static_cast<size_t>(vb), -1);
    out.sysPolyToHePoly.assign(static_cast<size_t>(pb), -1);

    // Collect region vertices
    std::unordered_set<int32_t> regionVerts;
    regionVerts.reserve(out.regionSysPolys.size() * 4u);

    int32_t totalLoops = 0;

    for (int32_t sp : out.regionSysPolys)
    {
        if (!valid_poly(sys, sp))
            continue;

        const SysPolyVerts& pv = sys->poly_verts(sp);
        totalLoops += pv.size();

        for (int32_t sv : pv)
        {
            if (valid_vert(sys, sv))
                regionVerts.insert(sv);
        }
    }

    // Deterministic ordering
    std::vector<int32_t> regionVertsSorted(regionVerts.begin(), regionVerts.end());
    std::sort(regionVertsSorted.begin(), regionVertsSorted.end());

    // Reserve
    out.mesh.reserve(static_cast<int32_t>(regionVertsSorted.size()),
                     totalLoops,
                     static_cast<int32_t>(out.regionSysPolys.size()),
                     totalLoops);

    // Create He verts (ONE He vert per Sys vert in region)
    for (int32_t sv : regionVertsSorted)
    {
        const glm::vec3& pos                         = sys->vert_position(sv);
        const int32_t    hv                          = out.mesh.createVert(pos);
        out.sysVertToHeVert[static_cast<size_t>(sv)] = hv;
    }

    // Build He->Sys vert mapping (only those that came from Sys)
    out.heVertToSysVert.assign(static_cast<size_t>(out.mesh.vertCount()), -1);
    for (int32_t sv : regionVertsSorted)
    {
        const int32_t hv = out.sysVertToHeVert[static_cast<size_t>(sv)];
        if (hv >= 0 && static_cast<size_t>(hv) < out.heVertToSysVert.size())
            out.heVertToSysVert[static_cast<size_t>(hv)] = sv;
    }

    // Editable set (Sys polys)
    std::unordered_set<int32_t> editableSet;
    editableSet.reserve(out.editableSysPolys.size());
    for (int32_t p : out.editableSysPolys)
        editableSet.insert(p);

    // Create He polys for region polys (editable + support)
    for (int32_t sp : out.regionSysPolys)
    {
        if (!valid_poly(sys, sp))
            continue;

        const SysPolyVerts& pv = sys->poly_verts(sp);

        std::vector<HeMesh::VertId> heVerts;
        heVerts.reserve(static_cast<size_t>(pv.size()));

        bool ok = true;
        for (int32_t sv : pv)
        {
            if (sv < 0 || static_cast<size_t>(sv) >= out.sysVertToHeVert.size())
            {
                ok = false;
                break;
            }

            const int32_t hv = out.sysVertToHeVert[static_cast<size_t>(sv)];
            if (hv < 0)
            {
                ok = false;
                break;
            }

            heVerts.push_back(static_cast<HeMesh::VertId>(hv));
        }

        if (!ok || heVerts.size() < 3)
            continue;

        const uint32_t       mat = sys->poly_material(sp);
        const HeMesh::PolyId hp  = out.mesh.createPoly(heVerts, mat);

        out.sysPolyToHePoly[static_cast<size_t>(sp)] = hp;
    }

    // He->Sys poly map + editable flags (ONLY for original extracted polys)
    out.hePolyToSysPoly.assign(static_cast<size_t>(out.mesh.polyCount()), -1);
    out.hePolyEditable.assign(static_cast<size_t>(out.mesh.polyCount()), 0);

    for (int32_t sp : out.regionSysPolys)
    {
        if (!valid_poly(sys, sp))
            continue;

        const int32_t hp = out.sysPolyToHePoly[static_cast<size_t>(sp)];
        if (hp < 0)
            continue;

        if (static_cast<size_t>(hp) < out.hePolyToSysPoly.size())
            out.hePolyToSysPoly[static_cast<size_t>(hp)] = sp;

        if (editableSet.contains(sp) && static_cast<size_t>(hp) < out.hePolyEditable.size())
            out.hePolyEditable[static_cast<size_t>(hp)] = 1;
    }

    // Optional face-varying import
    const int32_t normMap = sys->map_find(opt.normalMapId);
    const int32_t uvMap   = sys->map_find(opt.uvMapId);

    if (opt.importNormals || opt.importUVs)
    {
        for (int32_t sp : out.regionSysPolys)
        {
            if (!valid_poly(sys, sp))
                continue;

            const int32_t hp = out.sysPolyToHePoly[static_cast<size_t>(sp)];
            if (hp < 0)
                continue;

            auto                heLoops = out.mesh.polyLoops(hp);
            const SysPolyVerts& sysPV   = sys->poly_verts(sp);

            if (heLoops.size() != sysPV.size())
                continue;

            if (opt.importNormals && normMap >= 0 && sys->map_poly_valid(normMap, sp))
            {
                const SysPolyVerts& mn = sys->map_poly_verts(normMap, sp);
                if (mn.size() == sysPV.size())
                {
                    for (int32_t i = 0; i < mn.size(); ++i)
                    {
                        const float*    ptr = sys->map_vert_position(normMap, mn[i]);
                        const glm::vec3 n   = glm::make_vec3(ptr);
                        out.mesh.setLoopNormal(heLoops[i], n);
                    }
                }
            }

            if (opt.importUVs && uvMap >= 0 && sys->map_poly_valid(uvMap, sp))
            {
                const SysPolyVerts& mt = sys->map_poly_verts(uvMap, sp);
                if (mt.size() == sysPV.size())
                {
                    for (int32_t i = 0; i < mt.size(); ++i)
                    {
                        const float*    ptr = sys->map_vert_position(uvMap, mt[i]);
                        const glm::vec2 uv  = glm::make_vec2(ptr);
                        out.mesh.setLoopUV(heLoops[i], uv);
                    }
                }
            }
        }
    }

    return out;
}

// -----------------------------------------------------------------------------
// Commit building
// -----------------------------------------------------------------------------

HeMeshCommit build_commit_replace_editable(SysMesh*                   sys,
                                           const HeExtractionResult&  extract,
                                           const HeMesh&              finalHe,
                                           const HeExtractionOptions& opt)
{
    HeMeshCommit commit{};

    if (!sys)
        return commit;

    // Sys editable set (source of truth)
    std::unordered_set<int32_t> editableSysSet;
    editableSysSet.reserve(extract.editableSysPolys.size());
    for (int32_t p : extract.editableSysPolys)
        if (valid_poly(sys, p))
            editableSysSet.insert(p);

    // Remove ONLY editable Sys polys (stable: delete high->low)
    commit.removePolys = extract.editableSysPolys;
    sanitize_poly_list(sys, commit.removePolys);
    std::reverse(commit.removePolys.begin(), commit.removePolys.end());

    // Vert updates (move existing, create new)
    for (HeMesh::VertId hv : finalHe.allVerts())
    {
        const glm::vec3 pos = finalHe.position(hv);

        int32_t sysV = -1;
        if (hv >= 0 && static_cast<size_t>(hv) < extract.heVertToSysVert.size())
            sysV = extract.heVertToSysVert[static_cast<size_t>(hv)];

        if (sysV >= 0 && valid_vert(sys, sysV))
        {
            const glm::vec3& oldPos = sys->vert_position(sysV);
            if (!near_vec3(oldPos, pos))
                commit.moveVerts.push_back({sysV, pos});
        }
        else
        {
            commit.createVerts.push_back({hv, pos});
        }
    }

    // Recreate polys that are:
    //  - NEW (no mapped Sys poly)
    //  - OR mapped to a Sys poly that WAS editable
    for (HeMesh::PolyId hp : finalHe.allPolys())
    {
        int32_t mappedSysP = -1;
        if (hp >= 0 && static_cast<size_t>(hp) < extract.hePolyToSysPoly.size())
            mappedSysP = extract.hePolyToSysPoly[static_cast<size_t>(hp)];

        const bool isNew          = (mappedSysP < 0);
        const bool mappedEditable = (!isNew && editableSysSet.contains(mappedSysP));

        if (!isNew && !mappedEditable)
            continue;

        HeMeshCommit::CreatePoly cp{};
        cp.hePoly            = hp;
        cp.materialId        = finalHe.polyMaterial(hp);
        cp.selectAfterCreate = mappedEditable;

        const auto verts = finalHe.polyVerts(hp);
        const auto loops = finalHe.polyLoops(hp);

        cp.heVerts.reserve(static_cast<size_t>(verts.size()));
        for (int32_t i = 0; i < verts.size(); ++i)
            cp.heVerts.push_back(verts[i]);

        cp.hasNormals = opt.importNormals;
        cp.hasUVs     = opt.importUVs;

        if (cp.hasNormals)
        {
            cp.normals.reserve(static_cast<size_t>(loops.size()));
            const glm::vec3 fallback = finalHe.polyNormal(hp);

            for (int32_t i = 0; i < loops.size(); ++i)
            {
                const int32_t l = loops[i];
                cp.normals.push_back(finalHe.loopHasNormal(l) ? finalHe.loopNormal(l) : fallback);
            }
        }

        if (cp.hasUVs)
        {
            cp.uvs.reserve(static_cast<size_t>(loops.size()));
            for (int32_t i = 0; i < loops.size(); ++i)
            {
                const int32_t l = loops[i];
                cp.uvs.push_back(finalHe.loopHasUV(l) ? finalHe.loopUV(l) : glm::vec2(0.0f));
            }
        }

        // Defensive
        if (cp.hasNormals && cp.normals.size() != static_cast<size_t>(loops.size()))
            cp.hasNormals = false;
        if (cp.hasUVs && cp.uvs.size() != static_cast<size_t>(loops.size()))
            cp.hasUVs = false;

        commit.createPolys.push_back(std::move(cp));
    }

    return commit;
}

// Test
HeMeshCommit build_commit_replace_region(SysMesh*                   sys,
                                         const HeExtractionResult&  extract,
                                         const HeMesh&              finalHe,
                                         const HeExtractionOptions& opt)
{
    HeMeshCommit commit{};
    if (!sys)
        return commit;

    // Remove ALL region polys (editable + support)
    commit.removePolys = extract.regionSysPolys;

    // Same vert logic as editable builder (move existing, create new)
    auto near_vec3 = [](const glm::vec3& a, const glm::vec3& b, float eps = 1e-6f) noexcept {
        return glm::all(glm::epsilonEqual(a, b, eps));
    };

    for (HeMesh::VertId hv : finalHe.allVerts())
    {
        const glm::vec3 pos = finalHe.position(hv);

        int32_t sysV = -1;
        if (hv >= 0 && static_cast<size_t>(hv) < extract.heVertToSysVert.size())
            sysV = extract.heVertToSysVert[static_cast<size_t>(hv)];

        if (sysV >= 0 && sys->vert_valid(sysV))
        {
            const glm::vec3& oldPos = sys->vert_position(sysV);
            if (!near_vec3(oldPos, pos))
                commit.moveVerts.push_back({sysV, pos});
        }
        else
        {
            commit.createVerts.push_back({hv, pos});
        }
    }

    // Recreate ALL polys in finalHe that correspond to region polys or are new.
    for (HeMesh::PolyId hp : finalHe.allPolys())
    {
        int32_t mappedSysP = -1;
        if (hp >= 0 && static_cast<size_t>(hp) < extract.hePolyToSysPoly.size())
            mappedSysP = extract.hePolyToSysPoly[static_cast<size_t>(hp)];

        // keep any poly that mapped to sys but was NOT in region (shouldn't exist in finalHe normally)
        bool inRegion = false;
        if (mappedSysP >= 0)
        {
            // regionSysPolys is sorted in your extraction
            inRegion = std::binary_search(extract.regionSysPolys.begin(), extract.regionSysPolys.end(), mappedSysP);
        }

        const bool isNew = (mappedSysP < 0);

        if (!isNew && !inRegion)
            continue;

        HeMeshCommit::CreatePoly cp{};
        cp.hePoly     = hp;
        cp.materialId = finalHe.polyMaterial(hp);

        const auto verts = finalHe.polyVerts(hp);
        const auto loops = finalHe.polyLoops(hp);

        cp.heVerts.reserve(static_cast<size_t>(verts.size()));
        for (int32_t i = 0; i < verts.size(); ++i)
            cp.heVerts.push_back(verts[i]);

        cp.hasNormals = opt.importNormals;
        cp.hasUVs     = opt.importUVs;

        if (cp.hasNormals)
        {
            cp.normals.reserve(static_cast<size_t>(loops.size()));
            const glm::vec3 fallback = finalHe.polyNormal(hp);
            for (int32_t i = 0; i < loops.size(); ++i)
            {
                const int32_t l = loops[i];
                cp.normals.push_back(finalHe.loopHasNormal(l) ? finalHe.loopNormal(l) : fallback);
            }
        }

        if (cp.hasUVs)
        {
            cp.uvs.reserve(static_cast<size_t>(loops.size()));
            for (int32_t i = 0; i < loops.size(); ++i)
            {
                const int32_t l = loops[i];
                cp.uvs.push_back(finalHe.loopHasUV(l) ? finalHe.loopUV(l) : glm::vec2(0.0f));
            }
        }

        if (cp.hasNormals && cp.normals.size() != static_cast<size_t>(loops.size()))
            cp.hasNormals = false;
        if (cp.hasUVs && cp.uvs.size() != static_cast<size_t>(loops.size()))
            cp.hasUVs = false;

        commit.createPolys.push_back(std::move(cp));
    }

    return commit;
}

// -----------------------------------------------------------------------------
// Apply commit
// -----------------------------------------------------------------------------

void apply_commit(SysMesh*                   sys,
                  const HeExtractionResult&  extract,
                  const HeMeshCommit&        commit,
                  const HeExtractionOptions& opt)
{
    if (!sys)
        return;

    // 1) Remove editable polys (already sorted high->low)
    for (int32_t p : commit.removePolys)
        if (valid_poly(sys, p))
            sys->remove_poly(p);

    // 2) Move existing verts
    for (const auto& mv : commit.moveVerts)
        if (valid_vert(sys, mv.sysVert))
            sys->move_vert(mv.sysVert, mv.newPos);

    // 3) Create new verts
    std::unordered_map<int32_t, int32_t> heToSysNewVert;
    heToSysNewVert.reserve(commit.createVerts.size());

    for (const auto& cv : commit.createVerts)
    {
        const int32_t sv = sys->create_vert(cv.pos);
        heToSysNewVert.emplace(cv.heVert, sv);
    }

    auto resolveSysVert = [&](int32_t heVert) -> int32_t {
        if (heVert >= 0 && static_cast<size_t>(heVert) < extract.heVertToSysVert.size())
        {
            const int32_t sv = extract.heVertToSysVert[static_cast<size_t>(heVert)];
            if (sv >= 0)
                return sv;
        }

        const auto it = heToSysNewVert.find(heVert);
        if (it != heToSysNewVert.end())
            return it->second;

        return -1;
    };

    const int32_t sysNormMap = sys->map_find(opt.normalMapId);
    const int32_t sysUvMap   = sys->map_find(opt.uvMapId);

    bool wantsSelectionChange = false;
    for (const auto& cp : commit.createPolys)
    {
        if (cp.selectAfterCreate)
        {
            wantsSelectionChange = true;
            break;
        }
    }

    if (wantsSelectionChange)
        sys->clear_selected_polys();

    // 4) Create polys (+ map polys)
    for (const auto& cp : commit.createPolys)
    {
        SysPolyVerts pv;
        pv.reserve(static_cast<int32_t>(cp.heVerts.size()));

        bool ok = true;
        for (int32_t i = 0; i < static_cast<int32_t>(cp.heVerts.size()); ++i)
        {
            const int32_t sv = resolveSysVert(cp.heVerts[static_cast<size_t>(i)]);
            if (sv < 0)
            {
                ok = false;
                break;
            }
            pv.push_back(sv);
        }

        if (!ok || pv.size() < 3)
            continue;

        const int32_t newP = sys->create_poly(pv, cp.materialId);

        if (cp.selectAfterCreate)
            sys->select_poly(newP, true);

        if (cp.hasNormals && sysNormMap >= 0 && static_cast<int32_t>(cp.normals.size()) == pv.size())
        {
            SysPolyVerts mverts;
            mverts.reserve(pv.size());

            for (int32_t i = 0; i < pv.size(); ++i)
            {
                const glm::vec3 n  = cp.normals[static_cast<size_t>(i)];
                const int32_t   mv = sys->map_create_vert(sysNormMap, glm::value_ptr(n));
                mverts.push_back(mv);
            }

            sys->map_create_poly(sysNormMap, newP, mverts);
        }

        if (cp.hasUVs && sysUvMap >= 0 && static_cast<int32_t>(cp.uvs.size()) == pv.size())
        {
            SysPolyVerts mverts;
            mverts.reserve(pv.size());

            for (int32_t i = 0; i < pv.size(); ++i)
            {
                const glm::vec2 uv = cp.uvs[static_cast<size_t>(i)];
                const int32_t   mv = sys->map_create_vert(sysUvMap, glm::value_ptr(uv));
                mverts.push_back(mv);
            }

            sys->map_create_poly(sysUvMap, newP, mverts);
        }
    }
}
