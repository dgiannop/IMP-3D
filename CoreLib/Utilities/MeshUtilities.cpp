// #include <BoundingBox.hpp>
#include <MeshUtilities.hpp>
#include <SysMesh.hpp>
#include <SysObjLoader.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// #include "Tessellator.hpp"

// void triangulateMesh(SysMesh* mesh)
// {
//     Tessellator tess;
//     uint32_t    total = 0;

//     int norm_map = mesh->map_find(0);
//     int text_map = mesh->map_find(1);

//     for (int32_t poly_index : mesh->all_polys())
//     {
//         SysPolyVerts pv = mesh->poly_verts(poly_index);
//         SysPolyVerts pn = mesh->map_poly_verts(norm_map, poly_index);
//         SysPolyVerts pt = mesh->map_poly_verts(text_map, poly_index);

//         if (pv.size() != 3)
//         {
//             SmallList<glm::vec3, 4> points;
//             for (int i = 0; i < pv.size(); ++i)
//             {
//                 points.push_back(mesh->vert_position(pv[i]));
//             }

//             for (const auto& tri : triangulatePoly(tess, points, mesh->poly_normal(poly_index)))
//             {
//                 SysPolyVerts new_pv(3), new_pn(3), new_pt(3);

//                 for (int i = 0; i < 3; ++i)
//                 {
//                     new_pv[i] = pv[tri[i]];
//                     if (!pn.empty())
//                     {
//                         new_pn[i] = pn[tri[i]];
//                     }
//                     if (!pt.empty())
//                     {
//                         new_pt[i] = pt[tri[i]];
//                     }
//                 }
//                 int32_t new_mp = mesh->clone_poly(poly_index, new_pv);

//                 if (!pn.empty())
//                 {
//                     mesh->map_create_poly(norm_map, new_mp, new_pn);
//                 }
//                 if (!pt.empty())
//                 {
//                     mesh->map_create_poly(text_map, new_mp, new_pt);
//                 }
//                 total++;
//             }

//             mesh->remove_poly(poly_index);
//             if (mesh->map_poly_valid(norm_map, poly_index))
//             {
//                 mesh->map_remove_poly(norm_map, poly_index);
//             }
//             if (mesh->map_poly_valid(text_map, poly_index))
//             {
//                 mesh->map_remove_poly(text_map, poly_index);
//             }
//         }
//     }
//     std::cout << total << " polygons triangulated.\n";
// }

void centerMesh(SysMesh* mesh)
{
    // std::vector<glm::vec3> points;
    // for (int32_t vert_index : mesh->all_verts())
    // {
    //     points.push_back(mesh->vert_position(vert_index));
    // }
    // BoundingBox bbox = computeBoundingBox(points);

    // for (int32_t vert_index : mesh->all_verts())
    // {
    //     mesh->move_vert(vert_index, points[vert_index] - bbox.center);
    // }
}

void scaleMesh(SysMesh* mesh, float amount)
{
    // std::vector<glm::vec3> points;
    // for (int32_t vert_index : mesh->all_verts())
    // {
    //     points.push_back(mesh->vert_position(vert_index));
    // }
    // BoundingBox bbox = computeBoundingBox(points);

    // for (int32_t vert_index : mesh->all_verts())
    // {
    //     glm::vec3 pos = points[vert_index];
    //     pos -= bbox.center;
    //     pos = bbox.center + amount * pos;
    //     mesh->move_vert(vert_index, pos);
    // }
}

void checkMeshNormals(SysMesh* mesh)
{
    int32_t norm_map = mesh->map_find(0);

    for (int32_t poly_index : mesh->all_polys())
    {
        const SysPolyVerts& pv = mesh->poly_verts(poly_index);
        SysPolyVerts        pn = mesh->map_poly_verts(norm_map, poly_index);

        if (pn.size() != pv.size())
        {
            SysPolyVerts new_mp(pv.size());
            glm::vec3    norm = mesh->poly_normal(poly_index);
            for (int i = 0; i < pv.size(); ++i)
            {
                new_mp[i] = mesh->map_create_vert(norm_map, &norm[0]);
            }
            mesh->map_create_poly(norm_map, poly_index, new_mp);
        }
    }
}

MeshData extractMeshData(const SysMesh* mesh)
{
    MeshData out;

    if (!mesh)
        return out;

    const auto polyCount = mesh->num_polys();
    out.verts.reserve(static_cast<std::size_t>(polyCount) * 4);
    out.norms.reserve(static_cast<std::size_t>(polyCount) * 4);
    out.uvPos.reserve(static_cast<std::size_t>(polyCount) * 4);

    // Convention: map 0 = normals, map 1 = UVs
    const auto normMap = mesh->map_find(0);
    const auto uvMap   = mesh->map_find(1);

    // n-gon (v0..v{n-1}) → fan: (0,1,2), (0,2,3), ..., (0,n-2,n-1)
    for (int32_t polyIndex : mesh->all_polys())
    {
        const SysPolyVerts& pv = mesh->poly_verts(polyIndex);
        if (pv.size() < 3)
            continue; // degenerate, skip

        const SysPolyVerts& pn = mesh->map_poly_verts(normMap, polyIndex);
        const SysPolyVerts& pt = mesh->map_poly_verts(uvMap, polyIndex);

        // Fetch material ID for this polygon
        const uint32_t matId = mesh->poly_material(polyIndex);

        const int n = static_cast<int>(pv.size());

        for (int i = 1; i + 1 < n; ++i)
        {
            int cornerIndices[3] = {0, i, i + 1};

            for (int j = 0; j < 3; ++j)
            {
                const int localIndex = cornerIndices[j];

                // Position
                const int32_t vIdx = pv[localIndex];
                out.verts.push_back(mesh->vert_position(vIdx));

                // Normal: per-vertex map if present, otherwise flat poly normal
                if (pn.empty())
                {
                    out.norms.push_back(mesh->poly_normal(polyIndex));
                }
                else
                {
                    out.norms.push_back(
                        glm::make_vec3(mesh->map_vert_position(normMap, pn[localIndex])));
                }

                // UV: per-vertex map if present, otherwise 0,0
                if (!pt.empty())
                {
                    out.uvPos.push_back(
                        glm::make_vec2(mesh->map_vert_position(uvMap, pt[localIndex])));
                }
                else
                {
                    out.uvPos.emplace_back(0.0f, 0.0f);
                }

                // Material ID (one per vert)
                out.matIds.push_back(matId);
            }
        }
    }

    return out;
}

// new

std::vector<glm::vec3> extractTriPositionsOnly(const SysMesh* mesh)
{
    std::vector<glm::vec3> out;
    if (!mesh)
        return out;

    const auto polyCount = mesh->num_polys();
    out.reserve(static_cast<std::size_t>(polyCount) * 4);

    for (int32_t polyIndex : mesh->all_polys())
    {
        const SysPolyVerts& pv = mesh->poly_verts(polyIndex);
        if (pv.size() < 3)
            continue;

        const int n = static_cast<int>(pv.size());

        for (int i = 1; i + 1 < n; ++i)
        {
            const int32_t a = pv[0];
            const int32_t b = pv[i];
            const int32_t c = pv[i + 1];

            out.push_back(mesh->vert_position(a));
            out.push_back(mesh->vert_position(b));
            out.push_back(mesh->vert_position(c));
        }
    }

    return out;
}

std::vector<glm::vec3> extractMeshEdges(const SysMesh* mesh)
{
    std::vector<glm::vec3> out;

    if (!mesh)
        return out;

    const std::vector<IndexPair> edges = mesh->all_edges();
    out.reserve(edges.size() * 2);

    for (const IndexPair& e : edges)
    {
        const int32_t v0 = e.first;
        const int32_t v1 = e.second;

        out.push_back(mesh->vert_position(v0));
        out.push_back(mesh->vert_position(v1));
    }

    return out;
}

std::vector<uint32_t> extractMeshEdgeIndices(const SysMesh* mesh)
{
    std::vector<uint32_t> out;
    if (!mesh)
        return out;

    const std::vector<IndexPair> edges = mesh->all_edges();
    out.reserve(edges.size() * 2);

    for (const IndexPair& e : edges)
    {
        const int32_t v0 = e.first;
        const int32_t v1 = e.second;

        out.push_back(v0);
        out.push_back(v1);
    }

    return out;
}

std::vector<glm::vec3> extractMeshPositionsOnly(const SysMesh* sys)
{
    const uint32_t slotCount = sys->vert_buffer_size(); // HoleList slot count (0..capacity-1)

    std::vector<glm::vec3> uniqueVerts;
    uniqueVerts.resize(slotCount, glm::vec3(0.0f));

    for (uint32_t vi = 0; vi < slotCount; ++vi)
    {
        if (sys->vert_valid(vi))
            uniqueVerts[vi] = sys->vert_position(vi);
    }

    return uniqueVerts;
}

std::vector<uint32_t> extractMeshTriIndices(const SysMesh* sys)
{
    std::vector<uint32_t> out;
    if (!sys)
        return out;

    // Rough reserve: typical 4 verts per poly -> 2 tris -> 6 indices.
    out.reserve(static_cast<size_t>(sys->num_polys()) * 6u);

    for (int32_t polyIndex : sys->all_polys())
    {
        const SysPolyVerts& pv = sys->poly_verts(polyIndex);
        if (pv.size() < 3)
            continue;

        // Fan triangulation: (0,1,2), (0,2,3), ...
        for (size_t i = 1; i + 1 < pv.size(); ++i)
        {
            out.push_back(static_cast<uint32_t>(pv[0]));
            out.push_back(static_cast<uint32_t>(pv[i]));
            out.push_back(static_cast<uint32_t>(pv[i + 1]));
        }
    }

    return out;
}

std::vector<glm::vec3> extractPolyNormasOnly(const SysMesh* mesh)
{
    std::vector<glm::vec3> out;

    if (!mesh)
        return out;

    const auto polyCount = mesh->num_polys();
    out.reserve(static_cast<std::size_t>(polyCount) * 4);

    // Convention: map 0 = normals, map 1 = UVs
    const auto normMap = mesh->map_find(0);

    // n-gon (v0..v{n-1}) → fan: (0,1,2), (0,2,3), ..., (0,n-2,n-1)
    for (int32_t polyIndex : mesh->all_polys())
    {
        const SysPolyVerts& pv = mesh->poly_verts(polyIndex);
        if (pv.size() < 3)
            continue;

        const SysPolyVerts& pn = mesh->map_poly_verts(normMap, polyIndex);

        glm::vec3 polyNorm{0.f};
        if (pn.empty())
            polyNorm = mesh->poly_normal(polyIndex);

        const int n = static_cast<int>(pv.size());

        for (int i = 1; i + 1 < n; ++i)
        {
            int cornerIndices[3] = {0, i, i + 1};

            for (int j = 0; j < 3; ++j)
            {
                const int localIndex = cornerIndices[j];

                // Normal: per-vertex map if present, otherwise flat poly normal
                if (pn.empty())
                {
                    out.push_back(polyNorm);
                }
                else
                {
                    out.push_back(glm::make_vec3(mesh->map_vert_position(normMap, pn[localIndex])));
                }
            }
        }
    }

    return out;
}

std::vector<uint32_t> extractSelectedVertices(const SysMesh* sys)
{
    std::vector<uint32_t> out;

    const auto& sel = sys->selected_verts(); // TODO that can become const ref return and just return sys=?select....
    out.reserve(sel.size());

    for (uint32_t vi : sel)
        out.push_back(vi);

    return out;
}

std::vector<uint32_t> extractSelectedEdges(const SysMesh* sys)
{
    std::vector<uint32_t> out;
    out.reserve(sys->num_polys() * 4);

    for (IndexPair edge : sys->selected_edges())
    {
        const uint32_t v0 = edge.first;
        const uint32_t v1 = edge.second;

        out.push_back(v0);
        out.push_back(v1);
    }
    return out;
}

std::vector<uint32_t> extractSelectedPolyTriangles(const SysMesh* sys)
{
    std::vector<uint32_t> out;

    for (uint32_t pi : sys->selected_polys())
    {
        const auto poly = sys->poly_verts(pi); // list of vertex indices

        if (poly.size() < 3)
            continue;

        // Fan triangulation (simple and safe)
        for (size_t i = 1; i + 1 < poly.size(); ++i)
        {
            out.push_back(poly[0]);
            out.push_back(poly[i]);
            out.push_back(poly[i + 1]);
        }
    }
    return out;
}
