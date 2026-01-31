#include "Primitives.hpp"

#include <SysMesh.hpp>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <vector>

#include "AutoWelder.hpp"
#include "CoreUtilities.hpp"

namespace Primitives
{
    // Procedural box generator (hard-surface friendly):
    // - Creates a segmented box (sx,sy,sz)
    // - Shares POSITION verts via a single lattice grid
    // - Creates UVs face-varying (unique per corner) in a 3x4 cross layout
    // - Creates NORMALS face-varying (unique per corner), flat per-face by default
    // - Enforces outward CCW winding (UVs + normal-corners follow the final winding)
    void createBox(SysMesh* mesh, glm::vec3 center, glm::vec3 size, glm::ivec3 segs)
    {
        if (!mesh)
            return;

        const int sx = std::max(1, segs.x);
        const int sy = std::max(1, segs.y);
        const int sz = std::max(1, segs.z);

        const int SX = sx + 1;
        const int SY = sy + 1;
        const int SZ = sz + 1;

        const glm::vec3 pmin = center - 0.5f * size;
        const glm::vec3 pmax = center + 0.5f * size;

        // ---------------------------------------------------------------------
        // Maps (0 = normals (dim 3), 1 = uvs (dim 2))
        // ---------------------------------------------------------------------
        const int normMap = [&] {
            const int m = mesh->map_find(0);
            return (m >= 0) ? m : mesh->map_create(0, 0, 3);
        }();

        const int textMap = [&] {
            const int m = mesh->map_find(1);
            return (m >= 0) ? m : mesh->map_create(1, 0, 2);
        }();

        auto uv_in_cell = [&](glm::ivec2 cell, float u, float v) -> glm::vec2 {
            // 3x4 atlas cross
            const glm::vec2 base = glm::vec2(cell) / glm::vec2(3.0f, 4.0f);
            return base + glm::vec2(u / 3.0f, v / 4.0f);
        };

        // ---------------------------------------------------------------------
        // Shared lattice for POSITION verts (SoA would be separate; here AoS)
        // ---------------------------------------------------------------------
        std::vector<int32_t> grid(static_cast<size_t>(SX * SY * SZ), -1);

        auto Gref = [&](int x, int y, int z) -> int32_t& {
            return grid[static_cast<size_t>((z * SY + y) * SX + x)];
        };

        auto vert = [&](int x, int y, int z) -> int32_t {
            int32_t& id = Gref(x, y, z);
            if (id >= 0)
                return id;

            const float fx = static_cast<float>(x) / static_cast<float>(sx);
            const float fy = static_cast<float>(y) / static_cast<float>(sy);
            const float fz = static_cast<float>(z) / static_cast<float>(sz);

            const glm::vec3 p{
                pmin.x + fx * (pmax.x - pmin.x),
                pmin.y + fy * (pmax.y - pmin.y),
                pmin.z + fz * (pmax.z - pmin.z),
            };

            id = mesh->create_vert(p);
            return id;
        };

        // ---------------------------------------------------------------------
        // UV cells in your 3×4 cross layout
        // ---------------------------------------------------------------------
        constexpr glm::ivec2 UV_Xpos{2, 2}; // +X
        constexpr glm::ivec2 UV_Xneg{0, 2}; // -X
        constexpr glm::ivec2 UV_Ypos{1, 3}; // +Y
        constexpr glm::ivec2 UV_Yneg{1, 1}; // -Y
        constexpr glm::ivec2 UV_Zpos{1, 2}; // +Z
        constexpr glm::ivec2 UV_Zneg{1, 0}; // -Z

        // ---------------------------------------------------------------------
        // Helper: allocate a face-varying normal corner (dim=3) and uv corner (dim=2)
        // ---------------------------------------------------------------------
        auto create_nrm_corner = [&](const glm::vec3& n) -> int32_t {
            // map_create_vert expects float* to dim floats.
            return mesh->map_create_vert(normMap, glm::value_ptr(n));
        };

        auto create_uv_corner = [&](const glm::vec2& uv) -> int32_t {
            // Safe: vec2 is two floats contiguous.
            return mesh->map_create_vert(textMap, &uv.x);
        };

        // ---------------------------------------------------------------------
        // Emit a quad face with:
        // - base mesh poly verts [a b c d]
        // - per-corner normals (unique indices, but initialized flat)
        // - per-corner uvs (unique indices)
        // - outward CCW enforcement, with matching re-order of map verts
        // Parameters:
        //  a,b,c,d  : candidate vertex winding
        //  N        : intended outward face normal
        //  cell     : uv atlas cell
        //  u0,u1,v0,v1: face-local uv in [0,1]
        // ---------------------------------------------------------------------
        auto emit = [&](int32_t          a,
                        int32_t          b,
                        int32_t          c,
                        int32_t          d,
                        const glm::vec3& N,
                        glm::ivec2       cell,
                        float            u0,
                        float            u1,
                        float            v0,
                        float            v1) {
            // Build UV corners (pre-winding fix)
            glm::vec2 uvA = uv_in_cell(cell, u0, v0);
            glm::vec2 uvB = uv_in_cell(cell, u1, v0);
            glm::vec2 uvC = uv_in_cell(cell, u1, v1);
            glm::vec2 uvD = uv_in_cell(cell, u0, v1);

            // Flat normal corners (unique per corner, same vector to start)
            // These are face-varying by index, deformation by value.
            // We'll reorder the indices if winding flips.
            int32_t nA = create_nrm_corner(N);
            int32_t nB = create_nrm_corner(N);
            int32_t nC = create_nrm_corner(N);
            int32_t nD = create_nrm_corner(N);

            int32_t tA = create_uv_corner(uvA);
            int32_t tB = create_uv_corner(uvB);
            int32_t tC = create_uv_corner(uvC);
            int32_t tD = create_uv_corner(uvD);

            // Winding fix based on actual geometry:
            // Check if (b-a) x (d-a) points in same direction as N
            const glm::vec3 pa = mesh->vert_position(a);
            const glm::vec3 pb = mesh->vert_position(b);
            const glm::vec3 pd = mesh->vert_position(d);

            if (glm::dot(glm::cross(pb - pa, pd - pa), N) < 0.0f)
            {
                // Swap b <-> d to flip winding, and swap matching corners (B <-> D)
                std::swap(b, d);
                std::swap(uvB, uvD);
                std::swap(tB, tD);
                std::swap(nB, nD);
            }

            SysPolyVerts pv{a, b, c, d};
            SysPolyVerts nrm{nA, nB, nC, nD};
            SysPolyVerts uvs{tA, tB, tC, tD};

            const int32_t pid = mesh->create_poly(pv, 0);
            mesh->map_create_poly(normMap, pid, nrm);
            mesh->map_create_poly(textMap, pid, uvs);
        };

        // ---------------------------------------------------------------------
        // Build faces by iterating the volume cells, but emitting only boundary quads.
        // This matches your previous structure but produces proper face-varying normals.
        // ---------------------------------------------------------------------
        for (int x = 0; x < sx; ++x)
        {
            for (int y = 0; y < sy; ++y)
            {
                for (int z = 0; z < sz; ++z)
                {
                    const int32_t v000 = vert(x, y, z);
                    const int32_t v100 = vert(x + 1, y, z);
                    const int32_t v010 = vert(x, y + 1, z);
                    const int32_t v110 = vert(x + 1, y + 1, z);
                    const int32_t v001 = vert(x, y, z + 1);
                    const int32_t v101 = vert(x + 1, y, z + 1);
                    const int32_t v011 = vert(x, y + 1, z + 1);
                    const int32_t v111 = vert(x + 1, y + 1, z + 1);

                    // normalized segment coords per axis
                    const float ux0 = static_cast<float>(x) / static_cast<float>(sx);
                    const float ux1 = static_cast<float>(x + 1) / static_cast<float>(sx);

                    const float vy0 = static_cast<float>(y) / static_cast<float>(sy);
                    const float vy1 = static_cast<float>(y + 1) / static_cast<float>(sy);

                    const float wz0 = static_cast<float>(z) / static_cast<float>(sz);
                    const float wz1 = static_cast<float>(z + 1) / static_cast<float>(sz);

                    // +X face (u: -Z, v: +Y)
                    if (x == sx - 1)
                    {
                        emit(v100, v110, v111, v101, glm::vec3(+1.0f, 0.0f, 0.0f), UV_Xpos, 1.0f - wz1, 1.0f - wz0, vy0, vy1);
                    }

                    // -X face (u: +Z, v: +Y)
                    if (x == 0)
                    {
                        emit(v001, v011, v010, v000, glm::vec3(-1.0f, 0.0f, 0.0f), UV_Xneg, wz0, wz1, vy0, vy1);
                    }

                    // +Y face (u: +X, v: -Z)
                    if (y == sy - 1)
                    {
                        emit(v010, v110, v111, v011, glm::vec3(0.0f, +1.0f, 0.0f), UV_Ypos, ux0, ux1, 1.0f - wz1, 1.0f - wz0);
                    }

                    // -Y face (u: +X, v: +Z)
                    if (y == 0)
                    {
                        emit(v001, v101, v100, v000, glm::vec3(0.0f, -1.0f, 0.0f), UV_Yneg, ux0, ux1, wz0, wz1);
                    }

                    // +Z face (u: +X, v: +Y)
                    if (z == sz - 1)
                    {
                        emit(v001, v101, v111, v011, glm::vec3(0.0f, 0.0f, +1.0f), UV_Zpos, ux0, ux1, vy0, vy1);
                    }

                    // -Z face (u: -X, v: +Y)
                    if (z == 0)
                    {
                        emit(v010, v110, v100, v000, glm::vec3(0.0f, 0.0f, -1.0f), UV_Zneg, 1.0f - ux1, 1.0f - ux0, vy0, vy1);
                    }
                }
            }
        }
    }

    void createSphere(SysMesh* mesh, glm::vec3 center, glm::ivec3 axis, glm::vec3 radius, int rings, int sides, bool smooth)
    {
        AutoWelder welder;

        std::vector<int32_t>   verts;
        std::vector<glm::vec3> positions; // <— new: world positions for flat shading
        std::vector<glm::vec3> vnormals;  // per-vertex (smooth) normals
        std::vector<glm::vec2> uvs;

        // Build transforms (rotation + possibly non-uniform scale)
        glm::vec3 axis_f = glm::vec3(axis);
        glm::mat4 R      = glm::orientation(axis_f, glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 S      = glm::scale(glm::mat4(1.f), radius);
        glm::mat4 M      = R * S;                                      // model (no translation)
        glm::mat3 N      = glm::mat3(glm::transpose(glm::inverse(M))); // normal matrix

        // maps
        int normMap = [&] {
            int id = mesh->map_find(0);
            return (id >= 0) ? id : mesh->map_create(0, 0, 3);
        }();

        int textMap = [&] {
            int id = mesh->map_find(1);
            return (id >= 0) ? id : mesh->map_create(1, 0, 2);
        }();

        // Generate grid
        for (int stack = 0; stack <= rings; ++stack)
        {
            float phi = glm::half_pi<float>() - stack * glm::pi<float>() / rings;
            for (int slice = 0; slice <= sides; ++slice)
            {
                float theta = slice * 2.f * glm::pi<float>() / sides;

                glm::vec3 dir = {
                    -cos(phi) * sin(theta),
                    sin(phi),
                    -cos(phi) * cos(theta)};

                glm::vec3 pos = center + glm::vec3(M * glm::vec4(dir, 1.f));
                positions.push_back(pos);
                verts.push_back(welder(mesh, pos));

                glm::vec3 nrm = glm::normalize(N * dir); // per-vertex normal (for smooth)
                vnormals.push_back(nrm);

                uvs.emplace_back(1.f - float(slice) / sides, float(stack) / rings);
            }
        }

        auto emit_poly = [&](std::initializer_list<int> order) {
            // order contains indices into the grid (a/b/c/d variants)
            SysPolyVerts pv, uv, nr;

            // Build polygon in the given order
            std::vector<int> idx(order);
            for (int i : idx)
                pv.insert_unique(verts[i]);

            // UVs follow the same order
            for (int i : idx)
                uv.insert_unique(mesh->map_create_vert(textMap, &uvs[i].x));

            if (smooth)
            {
                // Per-vertex normals, same order
                for (int i : idx)
                    nr.insert_unique(mesh->map_create_vert(normMap, &vnormals[i].x));
            }
            else
            {
                // Per-face (flat) normal from polygon winding
                glm::vec3 p0 = positions[idx[0]];
                glm::vec3 p1 = positions[idx[1]];
                glm::vec3 p2 = positions[idx[2]];
                glm::vec3 fn = glm::normalize(glm::cross(p1 - p0, p2 - p0));

                // same normal at every corner
                for (size_t k = 0; k < idx.size(); ++k)
                    nr.insert_unique(mesh->map_create_vert(normMap, &fn.x));
            }

            int32_t poly_index = mesh->create_poly(pv, 0);
            mesh->map_create_poly(normMap, poly_index, nr);
            mesh->map_create_poly(textMap, poly_index, uv);
        };

        // Stitch quads/triangles
        for (int stack = 0; stack < rings; ++stack)
        {
            int idx_curr = stack * (sides + 1);
            int idx_next = (stack + 1) * (sides + 1);

            for (int slice = 0; slice < sides; ++slice)
            {
                int a = idx_curr + slice;
                int b = idx_next + slice;
                int c = idx_next + slice + 1;
                int d = idx_curr + slice + 1;

                if (stack == 0)
                {
                    // top cap triangle: (d, b, c) — CCW
                    emit_poly({d, b, c});
                }
                else if (stack == rings - 1)
                {
                    // bottom cap triangle: (a, b, d) — CCW
                    emit_poly({a, b, d});
                }
                else
                {
                    // middle quad: (b, c, d, a) — CCW
                    emit_poly({b, c, d, a});
                }
            }
        }
    }

    void createCylinder(SysMesh* mesh, glm::vec3 center, glm::ivec3 axis, float radius, float height, int sides, int segs, bool caps)
    {
        if (!mesh)
            return;

        sides = std::max(3, sides);
        segs  = std::max(1, segs);

        radius = std::max(0.0f, radius);
        height = std::max(0.0f, height);

        if (un::is_zero(radius) || un::is_zero(height))
            return;

        // ---------------------------------------------------------------------
        // Maps (0 = normals (dim 3), 1 = uvs (dim 2))
        // ---------------------------------------------------------------------
        const int normMap = [&] {
            const int m = mesh->map_find(0);
            return (m >= 0) ? m : mesh->map_create(0, 0, 3);
        }();

        const int textMap = [&] {
            const int m = mesh->map_find(1);
            return (m >= 0) ? m : mesh->map_create(1, 0, 2);
        }();

        // ---------------------------------------------------------------------
        // Axis frame (major axis only for now)
        // ---------------------------------------------------------------------
        glm::vec3 up = glm::vec3(axis);
        if (un::is_zero(up))
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        up = un::safe_normalize(up);

        glm::vec3 helper = (std::abs(up.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 xAxis = glm::cross(helper, up);
        if (un::is_zero(xAxis))
            xAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        xAxis = un::safe_normalize(xAxis);

        glm::vec3 zAxis = glm::cross(up, xAxis);
        if (un::is_zero(zAxis))
            zAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        zAxis = un::safe_normalize(zAxis);

        // ---------------------------------------------------------------------
        // Rings of shared POSITION verts:
        // (segs + 1) rings, each with 'sides' verts
        // ---------------------------------------------------------------------
        const int ringCount = segs + 1;

        std::vector<int32_t> ring(static_cast<size_t>(ringCount * sides), -1);

        auto Rref = [&](int r, int s) -> int32_t& {
            return ring[static_cast<size_t>(r * sides + s)];
        };

        // Precompute angles for stability
        std::vector<float> cosA(static_cast<size_t>(sides));
        std::vector<float> sinA(static_cast<size_t>(sides));

        for (int s = 0; s < sides; ++s)
        {
            const float t                = (static_cast<float>(s) / static_cast<float>(sides)) * (2.0f * glm::pi<float>());
            cosA[static_cast<size_t>(s)] = std::cos(t);
            sinA[static_cast<size_t>(s)] = std::sin(t);
        }

        // Build vertex rings
        for (int r = 0; r < ringCount; ++r)
        {
            const float fr = static_cast<float>(r) / static_cast<float>(segs);
            const float y  = (fr - 0.5f) * height;

            const glm::vec3 ringCenter = center + up * y;

            for (int s = 0; s < sides; ++s)
            {
                const glm::vec3 radial = xAxis * cosA[static_cast<size_t>(s)] + zAxis * sinA[static_cast<size_t>(s)];
                const glm::vec3 pos    = ringCenter + radial * radius;

                Rref(r, s) = mesh->create_vert(pos);
            }
        }

        // ---------------------------------------------------------------------
        // Emit side quad with face-varying UVs + normals
        // ---------------------------------------------------------------------
        auto emit_side = [&](int32_t a, int32_t b, int32_t c, int32_t d, const glm::vec3& nA, const glm::vec3& nB, float u0, float u1, float v0, float v1) {
            SysPolyVerts pv{a, b, c, d};

            const int32_t pid = mesh->create_poly(pv, 0);

            // UVs (side strip V in [0,0.5])
            const glm::vec2 uvA{u0, v0};
            const glm::vec2 uvB{u1, v0};
            const glm::vec2 uvC{u1, v1};
            const glm::vec2 uvD{u0, v1};

            SysPolyVerts uvs{
                mesh->map_create_vert(textMap, &uvA.x),
                mesh->map_create_vert(textMap, &uvB.x),
                mesh->map_create_vert(textMap, &uvC.x),
                mesh->map_create_vert(textMap, &uvD.x),
            };

            // Normals (radial, face-varying indices)
            SysPolyVerts nrms{
                mesh->map_create_vert(normMap, &nA.x),
                mesh->map_create_vert(normMap, &nB.x),
                mesh->map_create_vert(normMap, &nB.x),
                mesh->map_create_vert(normMap, &nA.x),
            };

            mesh->map_create_poly(textMap, pid, uvs);
            mesh->map_create_poly(normMap, pid, nrms);
        };

        // ---------------------------------------------------------------------
        // Side strip: V in [0, 0.5]
        // ---------------------------------------------------------------------
        for (int r = 0; r < segs; ++r)
        {
            const float v0 = (static_cast<float>(r) / static_cast<float>(segs)) * 0.5f;
            const float v1 = (static_cast<float>(r + 1) / static_cast<float>(segs)) * 0.5f;

            for (int s = 0; s < sides; ++s)
            {
                const int s1 = (s + 1) % sides;

                const int32_t a = Rref(r, s);
                const int32_t b = Rref(r, s1);
                const int32_t c = Rref(r + 1, s1);
                const int32_t d = Rref(r + 1, s);

                // Seam handling: last quad uses u1 = 1.0 instead of wrapping to 0.
                const float u0 = static_cast<float>(s) / static_cast<float>(sides);
                const float u1 = (s + 1 == sides) ? 1.0f : (static_cast<float>(s + 1) / static_cast<float>(sides));

                const glm::vec3 nA = un::safe_normalize(xAxis * cosA[static_cast<size_t>(s)] + zAxis * sinA[static_cast<size_t>(s)]);
                const glm::vec3 nB = un::safe_normalize(xAxis * cosA[static_cast<size_t>(s1)] + zAxis * sinA[static_cast<size_t>(s1)]);

                emit_side(a, b, c, d, nA, nB, u0, u1, v0, v1);
            }
        }

        if (!caps)
            return;

        // ---------------------------------------------------------------------
        // Caps: n-gons (face-varying UVs and normals), packed in top half V∈[0.5,1]
        // Bottom cap: left half (center at 0.25, 0.75)
        // Top cap:    right half (center at 0.75, 0.75)
        // ---------------------------------------------------------------------
        const float capR = 0.24f;

        auto emit_cap = [&](bool topCap) {
            SysPolyVerts pv = {};
            pv.reserve(sides);

            const int ringIndex = topCap ? segs : 0;

            if (topCap)
            {
                for (int s = 0; s < sides; ++s)
                    pv.push_back(Rref(ringIndex, s));
            }
            else
            {
                for (int i = 0; i < sides; ++i)
                {
                    const int s = (sides - 1) - i;
                    pv.push_back(Rref(ringIndex, s));
                }
            }

            const int32_t pid = mesh->create_poly(pv, 0);

            const glm::vec2 uvCenter = topCap ? glm::vec2(0.75f, 0.75f) : glm::vec2(0.25f, 0.75f);

            SysPolyVerts uvs = {};
            uvs.reserve(sides);

            if (topCap)
            {
                for (int s = 0; s < sides; ++s)
                {
                    const float u = uvCenter.x + cosA[static_cast<size_t>(s)] * capR;
                    const float v = uvCenter.y + sinA[static_cast<size_t>(s)] * capR;
                    glm::vec2   uv{u, v};
                    uvs.push_back(mesh->map_create_vert(textMap, &uv.x));
                }
            }
            else
            {
                for (int i = 0; i < sides; ++i)
                {
                    const int   s = (sides - 1) - i;
                    const float u = uvCenter.x + cosA[static_cast<size_t>(s)] * capR;
                    const float v = uvCenter.y + sinA[static_cast<size_t>(s)] * capR;
                    glm::vec2   uv{u, v};
                    uvs.push_back(mesh->map_create_vert(textMap, &uv.x));
                }
            }

            const glm::vec3 N = topCap ? up : -up;

            SysPolyVerts nrms = {};
            nrms.reserve(sides);
            for (int i = 0; i < sides; ++i)
                nrms.push_back(mesh->map_create_vert(normMap, &N.x));

            mesh->map_create_poly(textMap, pid, uvs);
            mesh->map_create_poly(normMap, pid, nrms);
        };

        emit_cap(false);
        emit_cap(true);
    }

    void createPlane(SysMesh* mesh, glm::vec3 center, glm::ivec3 axis, glm::vec2 size, glm::ivec2 segs)
    {
        if (!mesh)
            return;

        const int sx = std::max(1, segs.x);
        const int sy = std::max(1, segs.y);

        size.x = std::max(0.0f, size.x);
        size.y = std::max(0.0f, size.y);

        if (un::is_zero(size.x) || un::is_zero(size.y))
            return;

        // ---------------------------------------------------------------------
        // Maps (0 = normals (dim 3), 1 = uvs (dim 2))
        // ---------------------------------------------------------------------
        const int normMap = [&] {
            const int m = mesh->map_find(0);
            return (m >= 0) ? m : mesh->map_create(0, 0, 3);
        }();

        const int textMap = [&] {
            const int m = mesh->map_find(1);
            return (m >= 0) ? m : mesh->map_create(1, 0, 2);
        }();

        // ---------------------------------------------------------------------
        // Axis frame: axis is plane normal (major axis).
        // Build a stable (uAxis, vAxis) basis for the plane.
        // ---------------------------------------------------------------------
        glm::vec3 N = glm::vec3(axis);
        if (un::is_zero(N))
            N = glm::vec3(0.0f, 1.0f, 0.0f);
        N = un::safe_normalize(N);

        glm::vec3 helper = (std::abs(N.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 uAxis = glm::cross(helper, N);
        if (un::is_zero(uAxis))
            uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        uAxis = un::safe_normalize(uAxis);

        glm::vec3 vAxis = glm::cross(N, uAxis);
        if (un::is_zero(vAxis))
            vAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        vAxis = un::safe_normalize(vAxis);

        // Half extents along plane axes.
        const float hx = 0.5f * size.x;
        const float hy = 0.5f * size.y;

        // ---------------------------------------------------------------------
        // Shared lattice for POSITION verts (sx+1 by sy+1)
        // ---------------------------------------------------------------------
        const int SX = sx + 1;
        const int SY = sy + 1;

        std::vector<int32_t> grid(static_cast<size_t>(SX * SY), -1);

        auto Gref = [&](int x, int y) -> int32_t& {
            return grid[static_cast<size_t>(y * SX + x)];
        };

        auto vert = [&](int x, int y) -> int32_t {
            int32_t& id = Gref(x, y);
            if (id >= 0)
                return id;

            const float fx = static_cast<float>(x) / static_cast<float>(sx); // 0..1
            const float fy = static_cast<float>(y) / static_cast<float>(sy); // 0..1

            const float ox = (fx - 0.5f) * (2.0f * hx); // -hx..+hx
            const float oy = (fy - 0.5f) * (2.0f * hy); // -hy..+hy

            const glm::vec3 p = center + uAxis * ox + vAxis * oy;

            id = mesh->create_vert(p);
            return id;
        };

        auto create_nrm_corner = [&](const glm::vec3& n) -> int32_t {
            return mesh->map_create_vert(normMap, glm::value_ptr(n));
        };

        auto create_uv_corner = [&](const glm::vec2& uv) -> int32_t {
            return mesh->map_create_vert(textMap, &uv.x);
        };

        // ---------------------------------------------------------------------
        // Emit one quad cell with face-varying UVs/normals, enforcing CCW vs N.
        // a,b,c,d are candidate winding.
        // UVs are [0..1] across plane: u along +uAxis, v along +vAxis.
        // ---------------------------------------------------------------------
        auto emit = [&](int32_t a, int32_t b, int32_t c, int32_t d, float u0, float u1, float v0, float v1) {
            glm::vec2 uvA{u0, v0};
            glm::vec2 uvB{u1, v0};
            glm::vec2 uvC{u1, v1};
            glm::vec2 uvD{u0, v1};

            int32_t nA = create_nrm_corner(N);
            int32_t nB = create_nrm_corner(N);
            int32_t nC = create_nrm_corner(N);
            int32_t nD = create_nrm_corner(N);

            int32_t tA = create_uv_corner(uvA);
            int32_t tB = create_uv_corner(uvB);
            int32_t tC = create_uv_corner(uvC);
            int32_t tD = create_uv_corner(uvD);

            const glm::vec3 pa = mesh->vert_position(a);
            const glm::vec3 pb = mesh->vert_position(b);
            const glm::vec3 pd = mesh->vert_position(d);

            if (glm::dot(glm::cross(pb - pa, pd - pa), N) < 0.0f)
            {
                std::swap(b, d);
                std::swap(tB, tD);
                std::swap(nB, nD);
            }

            SysPolyVerts pv{a, b, c, d};
            SysPolyVerts nr{nA, nB, nC, nD};
            SysPolyVerts uv{tA, tB, tC, tD};

            const int32_t pid = mesh->create_poly(pv, 0);
            mesh->map_create_poly(normMap, pid, nr);
            mesh->map_create_poly(textMap, pid, uv);
        };

        // ---------------------------------------------------------------------
        // Build quads (sx * sy)
        // ---------------------------------------------------------------------
        for (int y = 0; y < sy; ++y)
        {
            const float v0 = static_cast<float>(y) / static_cast<float>(sy);
            const float v1 = static_cast<float>(y + 1) / static_cast<float>(sy);

            for (int x = 0; x < sx; ++x)
            {
                const float u0 = static_cast<float>(x) / static_cast<float>(sx);
                const float u1 = static_cast<float>(x + 1) / static_cast<float>(sx);

                const int32_t v00 = vert(x, y);
                const int32_t v10 = vert(x + 1, y);
                const int32_t v11 = vert(x + 1, y + 1);
                const int32_t v01 = vert(x, y + 1);

                // Candidate winding: v00 -> v10 -> v11 -> v01
                emit(v00, v10, v11, v01, u0, u1, v0, v1);
            }
        }
    }

} // namespace Primitives
