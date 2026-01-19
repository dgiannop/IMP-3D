#include "ImpSceneFormat.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

namespace
{
    static std::string trim(std::string s)
    {
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
            ++a;

        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
            --b;

        return s.substr(a, b - a);
    }

    static bool is_comment_or_empty(const std::string& raw)
    {
        const std::string s = trim(raw);
        if (s.empty())
            return true;
        if (s.rfind("#", 0) == 0)
            return true;
        if (s.rfind("//", 0) == 0)
            return true;
        return false;
    }

    static bool next_line(std::ifstream& in, std::string& outLine)
    {
        while (std::getline(in, outLine))
        {
            if (!outLine.empty() && outLine.back() == '\r')
                outLine.pop_back();

            if (!is_comment_or_empty(outLine))
                return true;
        }
        return false;
    }

    // Splits by whitespace, supports quoted strings: name "My Mesh"
    static std::vector<std::string> tokenize(const std::string& line)
    {
        std::vector<std::string> out;
        std::string              cur;
        bool                     inQuote = false;

        for (char c : line)
        {
            if (inQuote)
            {
                if (c == '"')
                {
                    inQuote = false;
                    out.push_back(cur);
                    cur.clear();
                }
                else
                {
                    cur.push_back(c);
                }
                continue;
            }

            if (c == '"')
            {
                inQuote = true;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(c)))
            {
                if (!cur.empty())
                {
                    out.push_back(cur);
                    cur.clear();
                }
                continue;
            }

            cur.push_back(c);
        }

        if (!cur.empty())
            out.push_back(cur);

        return out;
    }

    static bool parse_int32(const std::string& s, int32_t& v)
    {
        try
        {
            size_t    idx = 0;
            long long t   = std::stoll(s, &idx, 10);
            if (idx != s.size())
                return false;
            if (t < INT32_MIN || t > INT32_MAX)
                return false;
            v = static_cast<int32_t>(t);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool parse_uint32(const std::string& s, uint32_t& v)
    {
        try
        {
            size_t             idx = 0;
            unsigned long long t   = std::stoull(s, &idx, 10);
            if (idx != s.size())
                return false;
            if (t > 0xFFFFFFFFull)
                return false;
            v = static_cast<uint32_t>(t);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool parse_float(const std::string& s, float& v)
    {
        try
        {
            size_t idx = 0;
            float  t   = std::stof(s, &idx);
            if (idx != s.size())
                return false;
            v = t;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static void write_indent(std::ofstream& out, int indent)
    {
        for (int i = 0; i < indent; ++i)
            out << "    ";
    }

    // glm::mat4 is column-major; store as row-major for readability.
    static void mat4_to_row_major16(const glm::mat4& m, float out16[16]) noexcept
    {
        int k = 0;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out16[k++] = m[c][r];
    }

    static glm::mat4 row_major16_to_mat4(const float in16[16]) noexcept
    {
        glm::mat4 m{1.0f};
        int       k = 0;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                m[c][r] = in16[k++];
        return m;
    }

    // -------------------- MeshBlock for parsing --------------------

    struct MapBindingBlock
    {
        int32_t id   = -1;
        int32_t type = 0; // not really used yet; loader defaults to 0 anyway
        int32_t dim  = 0;

        // dense map verts: [denseIndex] -> float[dim]
        std::vector<std::vector<float>> mapVerts;

        struct PolyBind
        {
            int32_t              polyDenseIndex = -1;
            std::vector<int32_t> denseMapVertIndices; // size == poly vert count
        };
        std::vector<PolyBind> polyBinds;
    };

    struct MeshBlock
    {
        std::string name;
        bool        visible     = true;
        bool        selected    = true;
        int32_t     subdivLevel = 0;

        float modelRM[16] = {
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        std::vector<glm::vec3> verts;

        struct Poly
        {
            uint32_t             mat = 0;
            std::vector<int32_t> idx; // dense vertex indices
        };
        std::vector<Poly> polys;

        std::vector<MapBindingBlock> maps;
    };

    // Parse: map { ... }
    static bool parse_map(std::ifstream& in, MapBindingBlock& mb, SceneIOReport& report)
    {
        std::string line;

        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("Parse error: expected '{' after map");
            return false;
        }

        while (next_line(in, line))
        {
            const std::string s = trim(line);
            if (s == "}")
                break;

            const auto tok = tokenize(s);
            if (tok.empty())
                continue;

            const std::string& key = tok[0];

            if (key == "id")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.id))
                {
                    report.error("Parse error: invalid map id");
                    return false;
                }
                continue;
            }

            if (key == "type")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.type))
                {
                    report.error("Parse error: invalid map type");
                    return false;
                }
                continue;
            }

            if (key == "dim")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.dim) || mb.dim <= 0)
                {
                    report.error("Parse error: invalid map dim");
                    return false;
                }
                continue;
            }

            if (key == "map_verts")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("Parse error: expected '{' after map_verts");
                    return false;
                }

                mb.mapVerts.clear();

                while (next_line(in, line))
                {
                    const std::string vs = trim(line);
                    if (vs == "}")
                        break;

                    // mv <denseIndex> <f0> <f1> ...
                    const auto vt = tokenize(vs);
                    if (vt.size() < 3 || vt[0] != "mv")
                    {
                        report.error("Parse error: map_verts expects 'mv idx ...'");
                        return false;
                    }

                    int32_t denseIdx = -1;
                    if (!parse_int32(vt[1], denseIdx) || denseIdx < 0)
                    {
                        report.error("Parse error: invalid mv dense index");
                        return false;
                    }

                    if (mb.dim <= 0)
                    {
                        report.error("Parse error: map dim must be specified before map_verts");
                        return false;
                    }

                    if (static_cast<int32_t>(vt.size()) != 2 + mb.dim)
                    {
                        report.error("Parse error: mv float count does not match dim");
                        return false;
                    }

                    if (denseIdx >= static_cast<int32_t>(mb.mapVerts.size()))
                        mb.mapVerts.resize(static_cast<size_t>(denseIdx + 1));

                    std::vector<float> vec;
                    vec.resize(static_cast<size_t>(mb.dim));

                    for (int i = 0; i < mb.dim; ++i)
                    {
                        float f = 0.0f;
                        if (!parse_float(vt[2 + i], f))
                        {
                            report.error("Parse error: invalid float in mv");
                            return false;
                        }
                        vec[static_cast<size_t>(i)] = f;
                    }

                    mb.mapVerts[static_cast<size_t>(denseIdx)] = std::move(vec);
                }

                continue;
            }

            if (key == "poly_bindings")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("Parse error: expected '{' after poly_bindings");
                    return false;
                }

                mb.polyBinds.clear();

                while (next_line(in, line))
                {
                    const std::string ps = trim(line);
                    if (ps == "}")
                        break;

                    // mp <polyDenseIndex> <n> <mv0> <mv1> ...
                    const auto pt = tokenize(ps);
                    if (pt.size() < 4 || pt[0] != "mp")
                    {
                        report.error("Parse error: poly_bindings expects 'mp polyIdx n ...'");
                        return false;
                    }

                    int32_t polyIdx = -1;
                    int32_t n       = 0;
                    if (!parse_int32(pt[1], polyIdx) || polyIdx < 0 || !parse_int32(pt[2], n) || n < 3)
                    {
                        report.error("Parse error: invalid mp header");
                        return false;
                    }

                    if (static_cast<int32_t>(pt.size()) != 3 + n)
                    {
                        report.error("Parse error: mp token count mismatch");
                        return false;
                    }

                    MapBindingBlock::PolyBind bind{};
                    bind.polyDenseIndex = polyIdx;
                    bind.denseMapVertIndices.reserve(static_cast<size_t>(n));

                    for (int i = 0; i < n; ++i)
                    {
                        int32_t mv = -1;
                        if (!parse_int32(pt[3 + i], mv) || mv < 0)
                        {
                            report.error("Parse error: invalid map vert index in mp");
                            return false;
                        }
                        bind.denseMapVertIndices.push_back(mv);
                    }

                    mb.polyBinds.push_back(std::move(bind));
                }

                continue;
            }

            report.warning(std::string("Unknown map key ignored: '") + key + "'");
        }

        if (mb.id < 0)
        {
            report.error("Parse error: map missing id");
            return false;
        }

        if (mb.dim <= 0)
        {
            report.error("Parse error: map missing dim");
            return false;
        }

        return true;
    }

    static bool parse_mesh(std::ifstream& in, MeshBlock& mb, SceneIOReport& report)
    {
        std::string line;

        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("Parse error: expected '{' after mesh");
            return false;
        }

        int32_t declaredVertCount = -1;
        int32_t declaredPolyCount = -1;

        while (next_line(in, line))
        {
            const std::string s = trim(line);
            if (s == "}")
                break;

            const auto tok = tokenize(s);
            if (tok.empty())
                continue;

            const std::string& key = tok[0];

            if (key == "name")
            {
                if (tok.size() < 2)
                {
                    report.error("Parse error: name expects quoted string");
                    return false;
                }
                mb.name = tok[1];
                continue;
            }

            if (key == "visible")
            {
                if (tok.size() != 2)
                {
                    report.error("Parse error: visible expects 0 or 1");
                    return false;
                }
                int32_t v = 1;
                if (!parse_int32(tok[1], v))
                {
                    report.error("Parse error: invalid visible");
                    return false;
                }
                mb.visible = (v != 0);
                continue;
            }

            if (key == "selected")
            {
                if (tok.size() != 2)
                {
                    report.error("Parse error: selected expects 0 or 1");
                    return false;
                }
                int32_t v = 1;
                if (!parse_int32(tok[1], v))
                {
                    report.error("Parse error: invalid selected");
                    return false;
                }
                mb.selected = (v != 0);
                continue;
            }

            if (key == "subdiv_level")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.subdivLevel))
                {
                    report.error("Parse error: invalid subdiv_level");
                    return false;
                }
                continue;
            }

            if (key == "model_row_major")
            {
                if (tok.size() != 17)
                {
                    report.error("Parse error: model_row_major expects 16 floats");
                    return false;
                }
                for (int i = 0; i < 16; ++i)
                {
                    float f = 0.0f;
                    if (!parse_float(tok[1 + i], f))
                    {
                        report.error("Parse error: invalid float in model_row_major");
                        return false;
                    }
                    mb.modelRM[i] = f;
                }
                continue;
            }

            if (key == "vert_count")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], declaredVertCount) || declaredVertCount < 0)
                {
                    report.error("Parse error: invalid vert_count");
                    return false;
                }
                continue;
            }

            if (key == "verts")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("Parse error: expected '{' after verts");
                    return false;
                }

                mb.verts.clear();

                while (next_line(in, line))
                {
                    const std::string vs = trim(line);
                    if (vs == "}")
                        break;

                    const auto vt = tokenize(vs);
                    if (vt.size() != 4 || vt[0] != "v")
                    {
                        report.error("Parse error: verts expects 'v x y z'");
                        return false;
                    }

                    float x = 0, y = 0, z = 0;
                    if (!parse_float(vt[1], x) || !parse_float(vt[2], y) || !parse_float(vt[3], z))
                    {
                        report.error("Parse error: invalid float in vertex");
                        return false;
                    }

                    mb.verts.push_back(glm::vec3(x, y, z));
                }

                if (declaredVertCount >= 0 && static_cast<int32_t>(mb.verts.size()) != declaredVertCount)
                    report.warning("verts count mismatch with vert_count (continuing)");

                continue;
            }

            if (key == "poly_count")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], declaredPolyCount) || declaredPolyCount < 0)
                {
                    report.error("Parse error: invalid poly_count");
                    return false;
                }
                continue;
            }

            if (key == "polys")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("Parse error: expected '{' after polys");
                    return false;
                }

                mb.polys.clear();

                while (next_line(in, line))
                {
                    const std::string ps = trim(line);
                    if (ps == "}")
                        break;

                    // p <n> mat <matId> <i0> <i1> ...
                    const auto pt = tokenize(ps);
                    if (pt.size() < 6 || pt[0] != "p")
                    {
                        report.error("Parse error: poly expects 'p n mat matId i0 i1 ...'");
                        return false;
                    }

                    int32_t n = 0;
                    if (!parse_int32(pt[1], n) || n < 3)
                    {
                        report.error("Parse error: invalid polygon vertex count");
                        return false;
                    }

                    if (pt[2] != "mat")
                    {
                        report.error("Parse error: expected 'mat' in polygon line");
                        return false;
                    }

                    uint32_t matId = 0;
                    if (!parse_uint32(pt[3], matId))
                    {
                        report.error("Parse error: invalid material id");
                        return false;
                    }

                    const int expectedTokens = 1 + 1 + 1 + 1 + n;
                    if (static_cast<int>(pt.size()) != expectedTokens)
                    {
                        report.error("Parse error: polygon line token count mismatch");
                        return false;
                    }

                    MeshBlock::Poly poly{};
                    poly.mat = matId;
                    poly.idx.reserve(static_cast<size_t>(n));

                    for (int i = 0; i < n; ++i)
                    {
                        int32_t vi = 0;
                        if (!parse_int32(pt[4 + i], vi))
                        {
                            report.error("Parse error: invalid polygon index");
                            return false;
                        }
                        poly.idx.push_back(vi);
                    }

                    mb.polys.push_back(std::move(poly));
                }

                if (declaredPolyCount >= 0 && static_cast<int32_t>(mb.polys.size()) != declaredPolyCount)
                    report.warning("polys count mismatch with poly_count (continuing)");

                continue;
            }

            if (key == "maps")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("Parse error: expected '{' after maps");
                    return false;
                }

                mb.maps.clear();

                while (next_line(in, line))
                {
                    const std::string ms = trim(line);
                    if (ms == "}")
                        break;

                    if (ms != "map")
                    {
                        report.warning(std::string("Unknown maps entry ignored: '") + ms + "'");
                        continue;
                    }

                    MapBindingBlock mapb{};
                    if (!parse_map(in, mapb, report))
                        return false;

                    mb.maps.push_back(std::move(mapb));
                }

                continue;
            }

            report.warning(std::string("Unknown mesh key ignored: '") + key + "'");
        }

        if (mb.verts.empty())
        {
            report.error("Parse error: mesh has no verts");
            return false;
        }

        if (mb.polys.empty())
        {
            report.error("Parse error: mesh has no polys");
            return false;
        }

        return true;
    }

    // Heuristic: scan map IDs 0..kMaxMapId inclusive and treat those with map_find(id)!=-1 as existing.
    static std::vector<int32_t> discover_map_ids(const SysMesh* sys)
    {
        std::vector<int32_t> ids;
        if (!sys)
            return ids;

        // Keep it modest. If you ever need more, bump it.
        constexpr int32_t kMaxMapId = 31;

        for (int32_t id = 0; id <= kMaxMapId; ++id)
        {
            const int32_t map = sys->map_find(id);
            if (map != -1)
                ids.push_back(id);
        }

        return ids;
    }

} // namespace

bool ImpSceneFormat::save(const Scene* scene, const std::filesystem::path& filePath, const SaveOptions& options, SceneIOReport& report)
{
    if (!scene)
    {
        report.status = SceneIOStatus::InvalidScene;
        report.error("Save failed: scene is null");
        return false;
    }

    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        report.status = SceneIOStatus::WriteError;
        report.error("Save failed: could not open file for writing");
        return false;
    }

    const std::vector<SceneMesh*> meshes = scene->sceneMeshes();

    out << "imp_scene 2\n\n";

    for (const SceneMesh* sm : meshes)
    {
        if (!sm)
            continue;

        const SysMesh* sys = sm->sysMesh();
        if (!sys)
            continue;

        if (options.selectedOnly && !sm->selected())
            continue;

        const std::vector<int32_t>& vAll = sys->all_verts();
        const std::vector<int32_t>& pAll = sys->all_polys();

        if (vAll.empty() || pAll.empty())
            continue;

        // Dense vertex remap (SysMesh indices -> file dense indices)
        std::unordered_map<int32_t, int32_t> toDense;
        toDense.reserve(vAll.size());

        out << "mesh\n{\n";

        write_indent(out, 1);
        out << "name \"" << std::string(sm->name()) << "\"\n";

        write_indent(out, 1);
        out << "visible " << (sm->visible() ? 1 : 0) << "\n";
        write_indent(out, 1);
        out << "selected " << (sm->selected() ? 1 : 0) << "\n";

        write_indent(out, 1);
        out << "subdiv_level " << static_cast<int32_t>(sm->subdivisionLevel()) << "\n";

        {
            float m16[16]{};
            mat4_to_row_major16(sm->model(), m16);
            write_indent(out, 1);
            out << "model_row_major ";
            for (int i = 0; i < 16; ++i)
            {
                out << m16[i];
                if (i + 1 < 16)
                    out << " ";
            }
            out << "\n";
        }

        // verts
        write_indent(out, 1);
        out << "vert_count " << static_cast<int32_t>(vAll.size()) << "\n";
        write_indent(out, 1);
        out << "verts\n";
        write_indent(out, 1);
        out << "{\n";

        for (int32_t dense = 0; dense < static_cast<int32_t>(vAll.size()); ++dense)
        {
            const int32_t vi = vAll[static_cast<size_t>(dense)];
            toDense[vi]      = dense;

            const glm::vec3& p = sys->vert_position(vi);
            write_indent(out, 2);
            out << "v " << p.x << " " << p.y << " " << p.z << "\n";
        }

        write_indent(out, 1);
        out << "}\n\n";

        // polys (also record the written poly list order for map bindings)
        std::vector<int32_t> writtenPolys;
        writtenPolys.reserve(pAll.size());

        write_indent(out, 1);
        out << "poly_count " << static_cast<int32_t>(pAll.size()) << "\n";
        write_indent(out, 1);
        out << "polys\n";
        write_indent(out, 1);
        out << "{\n";

        for (int32_t pid : pAll)
        {
            if (!sys->poly_valid(pid))
                continue;

            const SysPolyVerts& pv = sys->poly_verts(pid);
            const int32_t       n  = static_cast<int32_t>(pv.size());
            if (n < 3)
                continue;

            writtenPolys.push_back(pid);

            const uint32_t mat = sys->poly_material(pid);

            write_indent(out, 2);
            out << "p " << n << " mat " << mat << " ";

            for (int32_t i = 0; i < n; ++i)
            {
                const int32_t srcVi = pv[static_cast<size_t>(i)];
                auto          it    = toDense.find(srcVi);
                out << (it != toDense.end() ? it->second : 0);

                if (i + 1 < n)
                    out << " ";
            }
            out << "\n";
        }

        write_indent(out, 1);
        out << "}\n\n";

        // -------------------- maps --------------------
        // No new SysMesh API: we probe IDs 0..31 and serialize those found.
        const std::vector<int32_t> mapIds = discover_map_ids(sys);
        if (!mapIds.empty())
        {
            write_indent(out, 1);
            out << "maps\n";
            write_indent(out, 1);
            out << "{\n";

            for (int32_t mapId : mapIds)
            {
                const int32_t map = sys->map_find(mapId);
                if (map == -1)
                    continue;

                const int32_t dim = sys->map_dim(map);
                if (dim <= 0)
                    continue;

                // Gather used map-vert ids by walking mapped polys
                std::vector<int32_t> usedMapVertIds;
                usedMapVertIds.reserve(1024);
                std::unordered_set<int32_t> seen;

                // Per-poly dense bindings to write later
                struct BindLine
                {
                    int32_t              polyDense = -1;
                    std::vector<int32_t> mapVertIds; // original mapVert IDs
                };
                std::vector<BindLine> binds;

                for (int32_t polyDense = 0; polyDense < static_cast<int32_t>(writtenPolys.size()); ++polyDense)
                {
                    const int32_t pid = writtenPolys[static_cast<size_t>(polyDense)];
                    if (!sys->map_poly_valid(map, pid))
                        continue;

                    const SysPolyVerts& pv  = sys->poly_verts(pid);
                    const SysPolyVerts& mpv = sys->map_poly_verts(map, pid);

                    if (mpv.size() != pv.size())
                        continue; // ignore partial/corrupt bindings

                    BindLine bl{};
                    bl.polyDense = polyDense;
                    bl.mapVertIds.reserve(mpv.size());

                    for (int32_t mvId : mpv)
                    {
                        bl.mapVertIds.push_back(mvId);
                        if (mvId >= 0 && !seen.count(mvId))
                        {
                            seen.insert(mvId);
                            usedMapVertIds.push_back(mvId);
                        }
                    }

                    binds.push_back(std::move(bl));
                }

                if (usedMapVertIds.empty() || binds.empty())
                    continue; // map exists but unused -> skip for now

                // Build dense map-vert remap: original mapVertId -> dense index
                std::unordered_map<int32_t, int32_t> mvToDense;
                mvToDense.reserve(usedMapVertIds.size());

                for (int32_t i = 0; i < static_cast<int32_t>(usedMapVertIds.size()); ++i)
                    mvToDense[usedMapVertIds[static_cast<size_t>(i)]] = i;

                // Write map block
                write_indent(out, 2);
                out << "map\n";
                write_indent(out, 2);
                out << "{\n";

                write_indent(out, 3);
                out << "id " << mapId << "\n";

                // Type is not observable via public API; keep field for future but default 0.
                write_indent(out, 3);
                out << "type 0\n";

                write_indent(out, 3);
                out << "dim " << dim << "\n\n";

                // map_verts
                write_indent(out, 3);
                out << "map_verts\n";
                write_indent(out, 3);
                out << "{\n";

                for (int32_t dense = 0; dense < static_cast<int32_t>(usedMapVertIds.size()); ++dense)
                {
                    const int32_t mvId = usedMapVertIds[static_cast<size_t>(dense)];
                    const float*  vec  = sys->map_vert_position(map, mvId);
                    if (!vec)
                        continue;

                    write_indent(out, 4);
                    out << "mv " << dense;

                    for (int k = 0; k < dim; ++k)
                        out << " " << vec[k];

                    out << "\n";
                }

                write_indent(out, 3);
                out << "}\n\n";

                // poly_bindings
                write_indent(out, 3);
                out << "poly_bindings\n";
                write_indent(out, 3);
                out << "{\n";

                for (const BindLine& bl : binds)
                {
                    const int32_t n = static_cast<int32_t>(bl.mapVertIds.size());
                    if (n < 3)
                        continue;

                    write_indent(out, 4);
                    out << "mp " << bl.polyDense << " " << n << " ";

                    for (int32_t i = 0; i < n; ++i)
                    {
                        const int32_t mvId = bl.mapVertIds[static_cast<size_t>(i)];
                        auto          it   = mvToDense.find(mvId);
                        out << (it != mvToDense.end() ? it->second : 0);

                        if (i + 1 < n)
                            out << " ";
                    }
                    out << "\n";
                }

                write_indent(out, 3);
                out << "}\n";

                write_indent(out, 2);
                out << "}\n\n";
            }

            write_indent(out, 1);
            out << "}\n";
        }

        out << "}\n\n";
    }

    if (!out.good())
    {
        report.status = SceneIOStatus::WriteError;
        report.error("Save failed: write error");
        return false;
    }

    report.status = SceneIOStatus::Ok;
    report.info("Saved .imp scene");
    return true;
}

bool ImpSceneFormat::load(Scene* scene, const std::filesystem::path& filePath, const LoadOptions& options, SceneIOReport& report)
{
    if (!scene)
    {
        report.status = SceneIOStatus::InvalidScene;
        report.error("Load failed: scene is null");
        return false;
    }

    std::ifstream in(filePath, std::ios::in);
    if (!in.is_open())
    {
        report.status = SceneIOStatus::FileNotFound;
        report.error("Load failed: file not found");
        return false;
    }

    if (!options.mergeIntoExisting)
        scene->clear();

    std::string line;

    if (!next_line(in, line))
    {
        report.error("Parse error: empty file");
        return false;
    }

    int32_t fileVer = 0;
    {
        const auto tok = tokenize(trim(line));
        if (tok.size() != 2 || tok[0] != "imp_scene")
        {
            report.status = SceneIOStatus::UnsupportedFormat;
            report.error("Parse error: missing 'imp_scene <version>' header");
            return false;
        }

        if (!parse_int32(tok[1], fileVer) || (fileVer != 1 && fileVer != 2))
        {
            report.status = SceneIOStatus::UnsupportedFormat;
            report.error("Unsupported .imp version");
            return false;
        }
    }

    while (next_line(in, line))
    {
        const std::string s = trim(line);
        if (s != "mesh")
        {
            report.warning(std::string("Unknown top-level key ignored: '") + s + "'");
            continue;
        }

        MeshBlock mb{};
        if (!parse_mesh(in, mb, report))
            return false;

        SceneMesh* sm = scene->createSceneMesh(mb.name);
        if (!sm)
        {
            report.error("Load failed: could not create SceneMesh");
            return false;
        }

        sm->visible(mb.visible);
        sm->selected(mb.selected);

        // Requires your one added setter:
        sm->model(row_major16_to_mat4(mb.modelRM));

        // Set subdiv level absolute via delta from current:
        sm->subdivisionLevel(mb.subdivLevel - sm->subdivisionLevel());

        SysMesh* sys = sm->sysMesh();
        if (!sys)
        {
            report.error("Load failed: SceneMesh has null SysMesh");
            return false;
        }

        sys->clear();

        // Create verts (dense order)
        std::vector<int32_t> newVertIds;
        newVertIds.reserve(mb.verts.size());

        for (const glm::vec3& p : mb.verts)
            newVertIds.push_back(sys->create_vert(p));

        // Create polys (keep created poly ids in file order for map bindings)
        std::vector<int32_t> createdPolyIds;
        createdPolyIds.reserve(mb.polys.size());

        for (const auto& p : mb.polys)
        {
            SysPolyVerts pv = {};
            for (int32_t denseIndex : p.idx)
            {
                if (denseIndex < 0 || denseIndex >= static_cast<int32_t>(newVertIds.size()))
                {
                    report.error("Parse error: polygon index out of range");
                    return false;
                }
                pv.push_back(newVertIds[static_cast<size_t>(denseIndex)]);
            }

            if (pv.size() >= 3)
            {
                const int32_t polyId = sys->create_poly(pv, p.mat);
                createdPolyIds.push_back(polyId);
            }
        }

        // Apply maps (v2 only; v1 simply has none)
        for (const MapBindingBlock& m : mb.maps)
        {
            if (m.id < 0 || m.dim <= 0)
                continue;

            // If map already exists, remove then recreate? For now, recreate cleanly.
            const int32_t existing = sys->map_find(m.id);
            if (existing != -1)
                sys->map_remove(m.id);

            const int32_t map = sys->map_create(m.id, 0 /*type*/, m.dim);
            if (map < 0)
            {
                report.warning("Failed to create map id " + std::to_string(m.id) + " (skipping)");
                continue;
            }

            // Recreate map verts in dense order (0..N-1)
            std::vector<int32_t> denseToMapVert;
            denseToMapVert.reserve(m.mapVerts.size());

            for (size_t i = 0; i < m.mapVerts.size(); ++i)
            {
                const auto& vec = m.mapVerts[i];
                if (static_cast<int32_t>(vec.size()) != m.dim)
                {
                    report.warning("map_verts entry dim mismatch; inserting zero vec");
                    std::vector<float> zero(static_cast<size_t>(m.dim), 0.0f);
                    denseToMapVert.push_back(sys->map_create_vert(map, zero.data()));
                    continue;
                }

                denseToMapVert.push_back(sys->map_create_vert(map, vec.data()));
            }

            // Bind mapped polys
            for (const auto& b : m.polyBinds)
            {
                if (b.polyDenseIndex < 0 || b.polyDenseIndex >= static_cast<int32_t>(createdPolyIds.size()))
                    continue;

                const int32_t polyId = createdPolyIds[static_cast<size_t>(b.polyDenseIndex)];
                if (!sys->poly_valid(polyId))
                    continue;

                const SysPolyVerts& pv = sys->poly_verts(polyId);
                if (static_cast<int32_t>(b.denseMapVertIndices.size()) != static_cast<int32_t>(pv.size()))
                    continue;

                SysPolyVerts mpv = {};
                mpv.reserve(pv.size());

                for (int32_t denseMv : b.denseMapVertIndices)
                {
                    if (denseMv < 0 || denseMv >= static_cast<int32_t>(denseToMapVert.size()))
                    {
                        // fall back to 0 if out of range
                        mpv.push_back(denseToMapVert.empty() ? -1 : denseToMapVert[0]);
                    }
                    else
                    {
                        mpv.push_back(denseToMapVert[static_cast<size_t>(denseMv)]);
                    }
                }

                sys->map_create_poly(map, polyId, mpv);
            }
        }
    }

    if (report.hasErrors())
        return false;

    report.status = SceneIOStatus::Ok;
    report.info("Loaded .imp scene");
    return true;
}
