#include "CmdSelectConnected.hpp"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    static uint64_t pack_undirected_i32(int32_t a, int32_t b) noexcept
    {
        if (a > b)
            std::swap(a, b);

        return (uint64_t(uint32_t(a)) << 32ull) | uint64_t(uint32_t(b));
    }

    static bool equal_sets_sorted(std::vector<int32_t>& a, std::vector<int32_t>& b) noexcept
    {
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        return a == b;
    }

    static bool equal_sets_sorted_edges(std::vector<IndexPair>& a, std::vector<IndexPair>& b) noexcept
    {
        auto norm = [](IndexPair& e) noexcept {
            if (e.first > e.second)
                std::swap(e.first, e.second);
        };

        for (IndexPair& e : a)
            norm(e);
        for (IndexPair& e : b)
            norm(e);

        auto cmp = [](const IndexPair& x, const IndexPair& y) noexcept {
            if (x.first != y.first)
                return x.first < y.first;
            return x.second < y.second;
        };

        std::sort(a.begin(), a.end(), cmp);
        std::sort(b.begin(), b.end(), cmp);
        return a == b;
    }

    static void select_connected_verts(SysMesh* mesh, bool& anyChanged)
    {
        const std::vector<int32_t>& sel = mesh->selected_verts();
        if (sel.empty())
            return;

        std::vector<uint8_t> mark = {};
        mark.resize(mesh->vert_buffer_size(), 0);

        std::queue<int32_t>  q;
        std::vector<int32_t> out = {};
        out.reserve(sel.size() * 4);

        for (int32_t v : sel)
        {
            if (!mesh->vert_valid(v))
                continue;

            const uint32_t uv = static_cast<uint32_t>(v);
            if (uv >= mark.size() || mark[uv])
                continue;

            mark[uv] = 1;
            q.push(v);
            out.push_back(v);
        }

        while (!q.empty())
        {
            const int32_t v = q.front();
            q.pop();

            const SysVertEdges& ve = mesh->vert_edges(v);

            for (const IndexPair& e : ve)
            {
                const int32_t a = e.first;
                const int32_t b = e.second;

                const int32_t other = (a == v) ? b : a;

                if (!mesh->vert_valid(other))
                    continue;

                const uint32_t uo = static_cast<uint32_t>(other);
                if (uo >= mark.size() || mark[uo])
                    continue;

                mark[uo] = 1;
                q.push(other);
                out.push_back(other);
            }
        }

        // Detect change vs existing selection
        std::vector<int32_t> before = sel;
        std::vector<int32_t> after  = out;

        if (!equal_sets_sorted(before, after))
        {
            mesh->clear_selected_verts();
            for (int32_t v : out)
            {
                if (mesh->vert_valid(v))
                    mesh->select_vert(v, true);
            }
            anyChanged = true;
        }
    }

    static void select_connected_edges(SysMesh* mesh, bool& anyChanged)
    {
        const std::vector<IndexPair>& sel = mesh->selected_edges();
        if (sel.empty())
            return;

        std::unordered_set<uint64_t> visited = {};
        visited.reserve(sel.size() * 4u);

        std::queue<IndexPair>  q;
        std::vector<IndexPair> out = {};
        out.reserve(sel.size() * 4);

        auto push_edge = [&](const IndexPair& e0) {
            const int32_t a = e0.first;
            const int32_t b = e0.second;

            if (!mesh->vert_valid(a) || !mesh->vert_valid(b))
                return;

            const uint64_t k = pack_undirected_i32(a, b);
            if (visited.find(k) != visited.end())
                return;

            visited.insert(k);
            q.push({a, b});
            out.push_back({a, b});
        };

        for (const IndexPair& e : sel)
            push_edge(e);

        while (!q.empty())
        {
            const IndexPair e = q.front();
            q.pop();

            const int32_t v0 = e.first;
            const int32_t v1 = e.second;

            // All edges incident to v0
            {
                const SysVertEdges& ve = mesh->vert_edges(v0);
                for (const IndexPair& ne : ve)
                    push_edge(ne);
            }

            // All edges incident to v1
            {
                const SysVertEdges& ve = mesh->vert_edges(v1);
                for (const IndexPair& ne : ve)
                    push_edge(ne);
            }
        }

        // Detect change vs existing selection
        std::vector<IndexPair> before = sel;
        std::vector<IndexPair> after  = out;

        if (!equal_sets_sorted_edges(before, after))
        {
            mesh->clear_selected_edges();
            for (const IndexPair& e : out)
            {
                if (!mesh->vert_valid(e.first) || !mesh->vert_valid(e.second))
                    continue;

                mesh->select_edge(e, true);
            }
            anyChanged = true;
        }
    }

    static void select_connected_polys(SysMesh* mesh, bool& anyChanged)
    {
        const std::vector<int32_t>& sel = mesh->selected_polys();
        if (sel.empty())
            return;

        std::vector<uint8_t> mark = {};
        mark.resize(mesh->poly_buffer_size(), 0);

        std::queue<int32_t>  q;
        std::vector<int32_t> out = {};
        out.reserve(sel.size() * 4);

        for (int32_t p : sel)
        {
            if (!mesh->poly_valid(p))
                continue;

            const uint32_t up = static_cast<uint32_t>(p);
            if (up >= mark.size() || mark[up])
                continue;

            mark[up] = 1;
            q.push(p);
            out.push_back(p);
        }

        while (!q.empty())
        {
            const int32_t p = q.front();
            q.pop();

            const SysPolyEdges& pe = mesh->poly_edges(p);

            for (const IndexPair& e : pe)
            {
                // Get polys on this edge, then flood to neighbors
                const SysEdgePolys ep = mesh->edge_polys(e);
                for (int32_t np : ep)
                {
                    if (!mesh->poly_valid(np))
                        continue;

                    const uint32_t unp = static_cast<uint32_t>(np);
                    if (unp >= mark.size() || mark[unp])
                        continue;

                    mark[unp] = 1;
                    q.push(np);
                    out.push_back(np);
                }
            }
        }

        // Detect change vs existing selection
        std::vector<int32_t> before = sel;
        std::vector<int32_t> after  = out;

        if (!equal_sets_sorted(before, after))
        {
            mesh->clear_selected_polys();
            for (int32_t p : out)
            {
                if (mesh->poly_valid(p))
                    mesh->select_poly(p, true);
            }
            anyChanged = true;
        }
    }
} // namespace

bool CmdSelectConnected::execute(Scene* scene)
{
    if (!scene)
        return false;

    bool anyChanged = false;

    const SelectionMode mode = scene->selectionMode();

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        switch (mode)
        {
            case SelectionMode::VERTS:
                select_connected_verts(mesh, anyChanged);
                break;

            case SelectionMode::EDGES:
                select_connected_edges(mesh, anyChanged);
                break;

            case SelectionMode::POLYS:
                select_connected_polys(mesh, anyChanged);
                break;
        }
    }

    return anyChanged;
}
