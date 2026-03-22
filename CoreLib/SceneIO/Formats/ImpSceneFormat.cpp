#include "ImpSceneFormat.hpp"

#include <LightHandler.hpp>
#include <SysMesh.hpp>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ImageHandler.hpp"
#include "Material.hpp"
#include "MaterialHandler.hpp"
#include "Scene.hpp"
#include "SceneLight.hpp"
#include "SceneMesh.hpp"
#include "SysMesh.hpp"

// ============================================================
// Internal helpers
// ============================================================
namespace
{

    // ------------------------------------------------------------
    // Base64
    // ------------------------------------------------------------
    static const char kB64Chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static std::string base64Encode(const uint8_t* data, size_t len)
    {
        std::string out;
        out.reserve(((len + 2) / 3) * 4);

        for (size_t i = 0; i < len; i += 3)
        {
            const uint32_t b0     = data[i];
            const uint32_t b1     = (i + 1 < len) ? data[i + 1] : 0u;
            const uint32_t b2     = (i + 2 < len) ? data[i + 2] : 0u;
            const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

            out.push_back(kB64Chars[(triple >> 18) & 0x3F]);
            out.push_back(kB64Chars[(triple >> 12) & 0x3F]);
            out.push_back((i + 1 < len) ? kB64Chars[(triple >> 6) & 0x3F] : '=');
            out.push_back((i + 2 < len) ? kB64Chars[(triple >> 0) & 0x3F] : '=');
        }

        return out;
    }

    static std::vector<uint8_t> base64Decode(const std::string& in)
    {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z')
                return c - 'A';
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 26;
            if (c >= '0' && c <= '9')
                return c - '0' + 52;
            if (c == '+')
                return 62;
            if (c == '/')
                return 63;
            return -1;
        };

        std::vector<uint8_t> out;
        out.reserve((in.size() / 4) * 3);

        for (size_t i = 0; i + 3 < in.size(); i += 4)
        {
            const int v0 = val(in[i]);
            const int v1 = val(in[i + 1]);
            const int v2 = val(in[i + 2]);
            const int v3 = val(in[i + 3]);

            if (v0 < 0 || v1 < 0)
                break;

            out.push_back(static_cast<uint8_t>((v0 << 2) | (v1 >> 4)));
            if (in[i + 2] != '=' && v2 >= 0)
                out.push_back(static_cast<uint8_t>(((v1 & 0xF) << 4) | (v2 >> 2)));
            if (in[i + 3] != '=' && v3 >= 0)
                out.push_back(static_cast<uint8_t>(((v2 & 0x3) << 6) | v3));
        }

        return out;
    }

    // ------------------------------------------------------------
    // Text helpers (shared with original)
    // ------------------------------------------------------------
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
        return s.empty() || s.rfind("#", 0) == 0 || s.rfind("//", 0) == 0;
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
                    cur.push_back(c);
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

    // ------------------------------------------------------------
    // Map block (unchanged from v2)
    // ------------------------------------------------------------
    struct MapBindingBlock
    {
        int32_t                         id   = -1;
        int32_t                         type = 0;
        int32_t                         dim  = 0;
        std::vector<std::vector<float>> mapVerts;
        struct PolyBind
        {
            int32_t              polyDenseIndex = -1;
            std::vector<int32_t> denseMapVertIndices;
        };
        std::vector<PolyBind> polyBinds;
    };

    struct MeshBlock
    {
        std::string            name;
        bool                   visible     = true;
        bool                   selected    = true;
        int32_t                subdivLevel = 0;
        float                  modelRM[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::vector<glm::vec3> verts;
        struct Poly
        {
            uint32_t             mat = 0;
            std::vector<int32_t> idx;
        };
        std::vector<Poly>            polys;
        std::vector<MapBindingBlock> maps;
    };

    // ------------------------------------------------------------
    // v3 data blocks
    // ------------------------------------------------------------
    struct ImageBlock
    {
        std::string key;        // stable key = relative path or name hint
        std::string path;       // relative path (empty = embedded)
        std::string base64Data; // non-empty when embedded
        // Raw pixel metadata — non-zero means data is raw RGBA/RGB pixels, not encoded PNG/JPEG/KTX.
        // Use createFromRaw() on load. KTX embedded images leave these as 0.
        int32_t width    = 0;
        int32_t height   = 0;
        int32_t channels = 0;
    };

    struct MaterialBlock
    {
        std::string name;
        glm::vec3   baseColor         = {1, 1, 1};
        float       opacity           = 1.f;
        float       roughness         = 0.5f;
        float       metallic          = 0.f;
        float       ior               = 1.5f;
        glm::vec3   emissiveColor     = {1, 1, 1};
        float       emissiveIntensity = 0.f;
        bool        doubleSided       = false;
        uint32_t    alphaMode         = 0; // 0=Opaque 1=Mask 2=Blend
        // texture keys (empty = none)
        std::string baseColorTex;
        std::string normalTex;
        std::string mraoTex;
        std::string metallicTex;
        std::string roughnessTex;
        std::string aoTex;
        std::string emissiveTex;
    };

    struct LightBlock
    {
        std::string name;
        uint32_t    type          = 1; // 0=Directional 1=Point 2=Spot
        glm::vec3   position      = {};
        glm::vec3   direction     = {0, 0, -1};
        glm::vec3   color         = {1, 1, 1};
        float       intensity     = 1.f;
        float       range         = 0.f;
        float       spotInnerCone = 0.f;
        float       spotOuterCone = 0.7853981633f;
        bool        enabled       = true;
        bool        affectRaster  = true;
        bool        affectRt      = true;
        bool        castShadows   = true;
        float       modelRM[16]   = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };

    // ------------------------------------------------------------
    // parse_map (unchanged)
    // ------------------------------------------------------------
    static bool parse_map(std::ifstream& in, MapBindingBlock& mb, SceneIOReport& report)
    {
        std::string line;
        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("expected '{' after map");
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
                    report.error("invalid map id");
                    return false;
                }
                continue;
            }
            if (key == "type")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.type))
                {
                    report.error("invalid map type");
                    return false;
                }
                continue;
            }
            if (key == "dim")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.dim) || mb.dim <= 0)
                {
                    report.error("invalid map dim");
                    return false;
                }
                continue;
            }

            if (key == "map_verts")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("expected '{' after map_verts");
                    return false;
                }
                mb.mapVerts.clear();
                while (next_line(in, line))
                {
                    const std::string vs = trim(line);
                    if (vs == "}")
                        break;
                    const auto vt = tokenize(vs);
                    if (vt.size() < 3 || vt[0] != "mv")
                    {
                        report.error("map_verts expects 'mv idx ...'");
                        return false;
                    }
                    int32_t denseIdx = -1;
                    if (!parse_int32(vt[1], denseIdx) || denseIdx < 0)
                    {
                        report.error("invalid mv dense index");
                        return false;
                    }
                    if (mb.dim <= 0)
                    {
                        report.error("map dim must be specified before map_verts");
                        return false;
                    }
                    if (static_cast<int32_t>(vt.size()) != 2 + mb.dim)
                    {
                        report.error("mv float count != dim");
                        return false;
                    }
                    if (denseIdx >= static_cast<int32_t>(mb.mapVerts.size()))
                        mb.mapVerts.resize(static_cast<size_t>(denseIdx + 1));
                    std::vector<float> vec(static_cast<size_t>(mb.dim));
                    for (int i = 0; i < mb.dim; ++i)
                    {
                        float f = 0;
                        if (!parse_float(vt[2 + i], f))
                        {
                            report.error("invalid float in mv");
                            return false;
                        }
                        vec[i] = f;
                    }
                    mb.mapVerts[static_cast<size_t>(denseIdx)] = std::move(vec);
                }
                continue;
            }

            if (key == "poly_bindings")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("expected '{' after poly_bindings");
                    return false;
                }
                mb.polyBinds.clear();
                while (next_line(in, line))
                {
                    const std::string ps = trim(line);
                    if (ps == "}")
                        break;
                    const auto pt = tokenize(ps);
                    if (pt.size() < 4 || pt[0] != "mp")
                    {
                        report.error("poly_bindings expects 'mp polyIdx n ...'");
                        return false;
                    }
                    int32_t polyIdx = -1, n = 0;
                    if (!parse_int32(pt[1], polyIdx) || polyIdx < 0 || !parse_int32(pt[2], n) || n < 3)
                    {
                        report.error("invalid mp header");
                        return false;
                    }
                    if (static_cast<int32_t>(pt.size()) != 3 + n)
                    {
                        report.error("mp token count mismatch");
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
                            report.error("invalid map vert index in mp");
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
            report.error("map missing id");
            return false;
        }
        if (mb.dim <= 0)
        {
            report.error("map missing dim");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------
    // parse_mesh (unchanged from v2)
    // ------------------------------------------------------------
    static bool parse_mesh(std::ifstream& in, MeshBlock& mb, SceneIOReport& report)
    {
        std::string line;
        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("expected '{' after mesh");
            return false;
        }

        int32_t declaredVerts = -1, declaredPolys = -1;

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
                    report.error("name expects string");
                    return false;
                }
                mb.name = tok[1];
                continue;
            }
            if (key == "visible")
            {
                int32_t v = 1;
                if (tok.size() != 2 || !parse_int32(tok[1], v))
                {
                    report.error("invalid visible");
                    return false;
                }
                mb.visible = (v != 0);
                continue;
            }
            if (key == "selected")
            {
                int32_t v = 1;
                if (tok.size() != 2 || !parse_int32(tok[1], v))
                {
                    report.error("invalid selected");
                    return false;
                }
                mb.selected = (v != 0);
                continue;
            }
            if (key == "subdiv_level")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], mb.subdivLevel))
                {
                    report.error("invalid subdiv_level");
                    return false;
                }
                continue;
            }

            if (key == "model_row_major")
            {
                if (tok.size() != 17)
                {
                    report.error("model_row_major expects 16 floats");
                    return false;
                }
                for (int i = 0; i < 16; ++i)
                {
                    float f = 0;
                    if (!parse_float(tok[1 + i], f))
                    {
                        report.error("invalid float in model_row_major");
                        return false;
                    }
                    mb.modelRM[i] = f;
                }
                continue;
            }

            if (key == "vert_count")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], declaredVerts) || declaredVerts < 0)
                {
                    report.error("invalid vert_count");
                    return false;
                }
                continue;
            }

            if (key == "verts")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("expected '{' after verts");
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
                        report.error("verts expects 'v x y z'");
                        return false;
                    }
                    float x = 0, y = 0, z = 0;
                    if (!parse_float(vt[1], x) || !parse_float(vt[2], y) || !parse_float(vt[3], z))
                    {
                        report.error("invalid float in vertex");
                        return false;
                    }
                    mb.verts.push_back(glm::vec3(x, y, z));
                }
                if (declaredVerts >= 0 && static_cast<int32_t>(mb.verts.size()) != declaredVerts)
                    report.warning("verts count mismatch (continuing)");
                continue;
            }

            if (key == "poly_count")
            {
                if (tok.size() != 2 || !parse_int32(tok[1], declaredPolys) || declaredPolys < 0)
                {
                    report.error("invalid poly_count");
                    return false;
                }
                continue;
            }

            if (key == "polys")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("expected '{' after polys");
                    return false;
                }
                mb.polys.clear();
                while (next_line(in, line))
                {
                    const std::string ps = trim(line);
                    if (ps == "}")
                        break;
                    const auto pt = tokenize(ps);
                    if (pt.size() < 6 || pt[0] != "p")
                    {
                        report.error("poly expects 'p n mat matId i0 i1 ...'");
                        return false;
                    }
                    int32_t n = 0;
                    if (!parse_int32(pt[1], n) || n < 3)
                    {
                        report.error("invalid polygon vertex count");
                        return false;
                    }
                    if (pt[2] != "mat")
                    {
                        report.error("expected 'mat' in polygon line");
                        return false;
                    }
                    uint32_t matId = 0;
                    if (!parse_uint32(pt[3], matId))
                    {
                        report.error("invalid material id");
                        return false;
                    }
                    if (static_cast<int>(pt.size()) != 1 + 1 + 1 + 1 + n)
                    {
                        report.error("polygon line token count mismatch");
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
                            report.error("invalid polygon index");
                            return false;
                        }
                        poly.idx.push_back(vi);
                    }
                    mb.polys.push_back(std::move(poly));
                }
                if (declaredPolys >= 0 && static_cast<int32_t>(mb.polys.size()) != declaredPolys)
                    report.warning("polys count mismatch (continuing)");
                continue;
            }

            if (key == "maps")
            {
                if (!next_line(in, line) || trim(line) != "{")
                {
                    report.error("expected '{' after maps");
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
                        report.warning(std::string("Unknown maps entry: '") + ms + "'");
                        continue;
                    }
                    MapBindingBlock mapb{};
                    if (!parse_map(in, mapb, report))
                        return false;
                    mb.maps.push_back(std::move(mapb));
                }
                continue;
            }
            report.warning(std::string("Unknown mesh key: '") + key + "'");
        }
        if (mb.verts.empty())
        {
            report.error("mesh has no verts");
            return false;
        }
        if (mb.polys.empty())
        {
            report.error("mesh has no polys");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------
    // parse_image (v3)
    // ------------------------------------------------------------
    static bool parse_image(std::ifstream& in, ImageBlock& ib, SceneIOReport& report)
    {
        std::string line;
        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("expected '{' after image");
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

            if (tok[0] == "key" && tok.size() >= 2)
            {
                ib.key = tok[1];
                continue;
            }
            if (tok[0] == "path" && tok.size() >= 2)
            {
                ib.path = tok[1];
                continue;
            }
            if (tok[0] == "data" && tok.size() >= 2)
            {
                ib.base64Data = tok[1];
                continue;
            }
            if (tok[0] == "width" && tok.size() == 2)
            {
                parse_int32(tok[1], ib.width);
                continue;
            }
            if (tok[0] == "height" && tok.size() == 2)
            {
                parse_int32(tok[1], ib.height);
                continue;
            }
            if (tok[0] == "channels" && tok.size() == 2)
            {
                parse_int32(tok[1], ib.channels);
                continue;
            }
            report.warning(std::string("Unknown image key: '") + tok[0] + "'");
        }
        if (ib.key.empty())
        {
            report.error("image missing key");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------
    // parse_material (v3)
    // ------------------------------------------------------------
    static bool parse_material(std::ifstream& in, MaterialBlock& mb, SceneIOReport& report)
    {
        std::string line;
        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("expected '{' after material");
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

            if (key == "name" && tok.size() >= 2)
            {
                mb.name = tok[1];
                continue;
            }
            if (key == "alpha_mode" && tok.size() == 2)
            {
                uint32_t v = 0;
                parse_uint32(tok[1], v);
                mb.alphaMode = v;
                continue;
            }
            if (key == "double_sided" && tok.size() == 2)
            {
                int32_t v = 0;
                parse_int32(tok[1], v);
                mb.doubleSided = (v != 0);
                continue;
            }
            if (key == "opacity" && tok.size() == 2)
            {
                parse_float(tok[1], mb.opacity);
                continue;
            }
            if (key == "roughness" && tok.size() == 2)
            {
                parse_float(tok[1], mb.roughness);
                continue;
            }
            if (key == "metallic" && tok.size() == 2)
            {
                parse_float(tok[1], mb.metallic);
                continue;
            }
            if (key == "ior" && tok.size() == 2)
            {
                parse_float(tok[1], mb.ior);
                continue;
            }
            if (key == "emissive_intensity" && tok.size() == 2)
            {
                parse_float(tok[1], mb.emissiveIntensity);
                continue;
            }
            if (key == "base_color" && tok.size() == 4)
            {
                parse_float(tok[1], mb.baseColor.r);
                parse_float(tok[2], mb.baseColor.g);
                parse_float(tok[3], mb.baseColor.b);
                continue;
            }
            if (key == "emissive_color" && tok.size() == 4)
            {
                parse_float(tok[1], mb.emissiveColor.r);
                parse_float(tok[2], mb.emissiveColor.g);
                parse_float(tok[3], mb.emissiveColor.b);
                continue;
            }
            if (key == "tex_base_color" && tok.size() == 2)
            {
                mb.baseColorTex = tok[1];
                continue;
            }
            if (key == "tex_normal" && tok.size() == 2)
            {
                mb.normalTex = tok[1];
                continue;
            }
            if (key == "tex_mrao" && tok.size() == 2)
            {
                mb.mraoTex = tok[1];
                continue;
            }
            if (key == "tex_metallic" && tok.size() == 2)
            {
                mb.metallicTex = tok[1];
                continue;
            }
            if (key == "tex_roughness" && tok.size() == 2)
            {
                mb.roughnessTex = tok[1];
                continue;
            }
            if (key == "tex_ao" && tok.size() == 2)
            {
                mb.aoTex = tok[1];
                continue;
            }
            if (key == "tex_emissive" && tok.size() == 2)
            {
                mb.emissiveTex = tok[1];
                continue;
            }
            report.warning(std::string("Unknown material key: '") + key + "'");
        }
        if (mb.name.empty())
        {
            report.error("material missing name");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------
    // parse_light (v3)
    // ------------------------------------------------------------
    static bool parse_light(std::ifstream& in, LightBlock& lb, SceneIOReport& report)
    {
        std::string line;
        if (!next_line(in, line) || trim(line) != "{")
        {
            report.error("expected '{' after light");
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

            if (key == "name" && tok.size() >= 2)
            {
                lb.name = tok[1];
                continue;
            }
            if (key == "type" && tok.size() == 2)
            {
                uint32_t v = 1;
                parse_uint32(tok[1], v);
                lb.type = v;
                continue;
            }
            if (key == "intensity" && tok.size() == 2)
            {
                parse_float(tok[1], lb.intensity);
                continue;
            }
            if (key == "range" && tok.size() == 2)
            {
                parse_float(tok[1], lb.range);
                continue;
            }
            if (key == "spot_inner_cone" && tok.size() == 2)
            {
                parse_float(tok[1], lb.spotInnerCone);
                continue;
            }
            if (key == "spot_outer_cone" && tok.size() == 2)
            {
                parse_float(tok[1], lb.spotOuterCone);
                continue;
            }
            if (key == "enabled" && tok.size() == 2)
            {
                int32_t v = 1;
                parse_int32(tok[1], v);
                lb.enabled = (v != 0);
                continue;
            }
            if (key == "affect_raster" && tok.size() == 2)
            {
                int32_t v = 1;
                parse_int32(tok[1], v);
                lb.affectRaster = (v != 0);
                continue;
            }
            if (key == "affect_rt" && tok.size() == 2)
            {
                int32_t v = 1;
                parse_int32(tok[1], v);
                lb.affectRt = (v != 0);
                continue;
            }
            if (key == "cast_shadows" && tok.size() == 2)
            {
                int32_t v = 1;
                parse_int32(tok[1], v);
                lb.castShadows = (v != 0);
                continue;
            }
            if (key == "position" && tok.size() == 4)
            {
                parse_float(tok[1], lb.position.x);
                parse_float(tok[2], lb.position.y);
                parse_float(tok[3], lb.position.z);
                continue;
            }
            if (key == "direction" && tok.size() == 4)
            {
                parse_float(tok[1], lb.direction.x);
                parse_float(tok[2], lb.direction.y);
                parse_float(tok[3], lb.direction.z);
                continue;
            }
            if (key == "color" && tok.size() == 4)
            {
                parse_float(tok[1], lb.color.r);
                parse_float(tok[2], lb.color.g);
                parse_float(tok[3], lb.color.b);
                continue;
            }
            if (key == "model_row_major" && tok.size() == 17)
            {
                for (int i = 0; i < 16; ++i)
                {
                    float f = 0;
                    parse_float(tok[1 + i], f);
                    lb.modelRM[i] = f;
                }
                continue;
            }
            report.warning(std::string("Unknown light key: '") + key + "'");
        }
        return true;
    }

    // Probe IDs 0..31 for existing maps
    static std::vector<int32_t> discover_map_ids(const SysMesh* sys)
    {
        std::vector<int32_t> ids;
        if (!sys)
            return ids;
        for (int32_t id = 0; id <= 31; ++id)
            if (sys->map_find(id) != -1)
                ids.push_back(id);
        return ids;
    }

} // namespace

// ============================================================
// SAVE
// ============================================================
bool ImpSceneFormat::save(const Scene*                 scene,
                          const std::filesystem::path& filePath,
                          const SaveOptions&           options,
                          SceneIOReport&               report)
{
    if (!scene)
    {
        report.status = SceneIOStatus::InvalidScene;
        report.error("scene is null");
        return false;
    }

    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        report.status = SceneIOStatus::WriteError;
        report.error("could not open file for writing");
        return false;
    }

    const std::filesystem::path baseDir = filePath.parent_path();

    out << "imp_scene 3\n\n";

    // --------------------------------------------------------
    // Images
    // --------------------------------------------------------
    const ImageHandler* ih = scene->imageHandler();
    if (ih && !ih->images().empty())
    {
        out << "images\n{\n";

        for (const Image& img : ih->images())
        {
            if (!img.valid())
                continue;

            // Key: use relative path if available, else name
            std::string key;
            if (!img.path().empty())
            {
                try
                {
                    key = std::filesystem::relative(img.path(), baseDir).string();
                }
                catch (...)
                {
                    key = img.path().string();
                }
            }
            else
            {
                key = img.name().empty() ? "image" : img.name();
            }

            // Normalize key separators
            for (char& c : key)
                if (c == '\\')
                    c = '/';

            write_indent(out, 1);
            out << "image\n";
            write_indent(out, 1);
            out << "{\n";
            write_indent(out, 2);
            out << "key \"" << key << "\"\n";

            if (!img.path().empty() && std::filesystem::exists(img.path()))
            {
                // External — store relative path
                std::string relPath;
                try
                {
                    relPath = std::filesystem::relative(img.path(), baseDir).string();
                }
                catch (...)
                {
                    relPath = img.path().string();
                }
                for (char& c : relPath)
                    if (c == '\\')
                        c = '/';
                write_indent(out, 2);
                out << "path \"" << relPath << "\"\n";
            }
            else
            {
                // Embedded — base64-encode raw pixels or KTX data
                if (img.isKtx() && !img.ktxData().empty())
                {
                    const std::string b64 = base64Encode(img.ktxData().data(), img.ktxData().size());
                    write_indent(out, 2);
                    out << "data " << b64 << "\n";
                }
                else if (img.data() && img.width() > 0 && img.height() > 0)
                {
                    // Raw decoded pixels — store dimensions so load can call createFromRaw.
                    const size_t pixBytes = static_cast<size_t>(img.width()) *
                                            static_cast<size_t>(img.height()) *
                                            static_cast<size_t>(img.channels());
                    const std::string b64 = base64Encode(img.data(), pixBytes);
                    write_indent(out, 2);
                    out << "width " << img.width() << "\n";
                    write_indent(out, 2);
                    out << "height " << img.height() << "\n";
                    write_indent(out, 2);
                    out << "channels " << img.channels() << "\n";
                    write_indent(out, 2);
                    out << "data " << b64 << "\n";
                }
            }

            write_indent(out, 1);
            out << "}\n";
        }

        out << "}\n\n";
    }

    // Build imageId -> key map for material texture references
    std::unordered_map<ImageId, std::string> imageKeyMap;
    if (ih)
    {
        for (ImageId id = 0; id < static_cast<ImageId>(ih->images().size()); ++id)
        {
            const Image* img = ih->get(id);
            if (!img || !img->valid())
                continue;
            std::string key;
            if (!img->path().empty())
            {
                try
                {
                    key = std::filesystem::relative(img->path(), baseDir).string();
                }
                catch (...)
                {
                    key = img->path().string();
                }
                for (char& c : key)
                    if (c == '\\')
                        c = '/';
            }
            else
            {
                key = img->name().empty() ? ("image_" + std::to_string(id)) : img->name();
            }
            imageKeyMap[id] = key;
        }
    }

    // --------------------------------------------------------
    // Materials
    // --------------------------------------------------------
    const MaterialHandler* mh = scene->materialHandler();
    if (mh && !mh->materials().empty())
    {
        out << "materials\n{\n";

        for (const Material& mat : mh->materials())
        {
            write_indent(out, 1);
            out << "material\n";
            write_indent(out, 1);
            out << "{\n";

            write_indent(out, 2);
            out << "name \"" << mat.name() << "\"\n";
            write_indent(out, 2);
            out << "alpha_mode " << static_cast<uint32_t>(mat.alphaMode()) << "\n";
            write_indent(out, 2);
            out << "double_sided " << (mat.doubleSided() ? 1 : 0) << "\n";
            write_indent(out, 2);
            out << "opacity " << mat.opacity() << "\n";
            write_indent(out, 2);
            out << "roughness " << mat.roughness() << "\n";
            write_indent(out, 2);
            out << "metallic " << mat.metallic() << "\n";
            write_indent(out, 2);
            out << "ior " << mat.ior() << "\n";
            write_indent(out, 2);
            out << "emissive_intensity " << mat.emissiveIntensity() << "\n";

            const glm::vec3& bc = mat.baseColor();
            write_indent(out, 2);
            out << "base_color " << bc.r << " " << bc.g << " " << bc.b << "\n";
            const glm::vec3& ec = mat.emissiveColor();
            write_indent(out, 2);
            out << "emissive_color " << ec.r << " " << ec.g << " " << ec.b << "\n";

            auto writeTex = [&](const char* field, ImageId id) {
                if (id == kInvalidImageId)
                    return;
                auto it = imageKeyMap.find(id);
                if (it == imageKeyMap.end())
                    return;
                write_indent(out, 2);
                out << field << " \"" << it->second << "\"\n";
            };

            writeTex("tex_base_color", mat.baseColorTexture());
            writeTex("tex_normal", mat.normalTexture());
            writeTex("tex_mrao", mat.mraoTexture());
            writeTex("tex_metallic", mat.metallicTexture());
            writeTex("tex_roughness", mat.roughnessTexture());
            writeTex("tex_ao", mat.aoTexture());
            writeTex("tex_emissive", mat.emissiveTexture());

            write_indent(out, 1);
            out << "}\n";
        }

        out << "}\n\n";
    }

    // --------------------------------------------------------
    // Lights
    // --------------------------------------------------------
    {
        const std::vector<SceneLight*> lights = scene->sceneLights();
        if (!lights.empty())
        {
            out << "lights\n{\n";

            for (const SceneLight* sl : lights)
            {
                if (!sl)
                    continue;

                write_indent(out, 1);
                out << "light\n";
                write_indent(out, 1);
                out << "{\n";

                write_indent(out, 2);
                out << "name \"" << std::string(sl->name()) << "\"\n";
                write_indent(out, 2);
                out << "type " << static_cast<uint32_t>(sl->lightType()) << "\n";
                write_indent(out, 2);
                out << "enabled " << (sl->enabled() ? 1 : 0) << "\n";
                write_indent(out, 2);
                out << "affect_raster " << (sl->affectRaster() ? 1 : 0) << "\n";
                write_indent(out, 2);
                out << "affect_rt " << (sl->affectRt() ? 1 : 0) << "\n";
                write_indent(out, 2);
                out << "cast_shadows " << (sl->castShadows() ? 1 : 0) << "\n";
                write_indent(out, 2);
                out << "intensity " << sl->intensity() << "\n";
                write_indent(out, 2);
                out << "range " << sl->range() << "\n";
                write_indent(out, 2);
                out << "spot_inner_cone " << sl->spotInnerConeRad() << "\n";
                write_indent(out, 2);
                out << "spot_outer_cone " << sl->spotOuterConeRad() << "\n";

                const glm::vec3 pos = sl->position();
                write_indent(out, 2);
                out << "position " << pos.x << " " << pos.y << " " << pos.z << "\n";
                const glm::vec3 dir = sl->direction();
                write_indent(out, 2);
                out << "direction " << dir.x << " " << dir.y << " " << dir.z << "\n";
                const glm::vec3 col = sl->color();
                write_indent(out, 2);
                out << "color " << col.r << " " << col.g << " " << col.b << "\n";

                float m16[16]{};
                mat4_to_row_major16(sl->model(), m16);
                write_indent(out, 2);
                out << "model_row_major ";
                for (int i = 0; i < 16; ++i)
                {
                    out << m16[i];
                    if (i + 1 < 16)
                        out << " ";
                }
                out << "\n";

                write_indent(out, 1);
                out << "}\n";
            }

            out << "}\n\n";
        }
    }

    // --------------------------------------------------------
    // Meshes (unchanged from v2)
    // --------------------------------------------------------
    for (const SceneMesh* sm : scene->sceneMeshes())
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

        write_indent(out, 1);
        out << "vert_count " << static_cast<int32_t>(vAll.size()) << "\n";
        write_indent(out, 1);
        out << "verts\n";
        write_indent(out, 1);
        out << "{\n";
        for (int32_t dense = 0; dense < static_cast<int32_t>(vAll.size()); ++dense)
        {
            const int32_t vi   = vAll[static_cast<size_t>(dense)];
            toDense[vi]        = dense;
            const glm::vec3& p = sys->vert_position(vi);
            write_indent(out, 2);
            out << "v " << p.x << " " << p.y << " " << p.z << "\n";
        }
        write_indent(out, 1);
        out << "}\n\n";

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
            write_indent(out, 2);
            out << "p " << n << " mat " << sys->poly_material(pid) << " ";
            for (int32_t i = 0; i < n; ++i)
            {
                auto it = toDense.find(pv[static_cast<size_t>(i)]);
                out << (it != toDense.end() ? it->second : 0);
                if (i + 1 < n)
                    out << " ";
            }
            out << "\n";
        }
        write_indent(out, 1);
        out << "}\n\n";

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

                std::vector<int32_t> usedMapVertIds;
                usedMapVertIds.reserve(1024);
                std::unordered_set<int32_t> seen;

                struct BindLine
                {
                    int32_t              polyDense = -1;
                    std::vector<int32_t> mapVertIds;
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
                        continue;
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
                    continue;

                std::unordered_map<int32_t, int32_t> mvToDense;
                mvToDense.reserve(usedMapVertIds.size());
                for (int32_t i = 0; i < static_cast<int32_t>(usedMapVertIds.size()); ++i)
                    mvToDense[usedMapVertIds[static_cast<size_t>(i)]] = i;

                write_indent(out, 2);
                out << "map\n";
                write_indent(out, 2);
                out << "{\n";
                write_indent(out, 3);
                out << "id " << mapId << "\n";
                write_indent(out, 3);
                out << "type 0\n";
                write_indent(out, 3);
                out << "dim " << dim << "\n\n";

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
                        auto it = mvToDense.find(bl.mapVertIds[static_cast<size_t>(i)]);
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
        report.error("write error");
        return false;
    }
    report.status = SceneIOStatus::Ok;
    report.info("Saved .imp v3 scene");
    return true;
}

// ============================================================
// LOAD
// ============================================================
bool ImpSceneFormat::load(Scene*                       scene,
                          const std::filesystem::path& filePath,
                          const LoadOptions&           options,
                          SceneIOReport&               report)
{
    if (!scene)
    {
        report.status = SceneIOStatus::InvalidScene;
        report.error("scene is null");
        return false;
    }

    std::ifstream in(filePath, std::ios::in);
    if (!in.is_open())
    {
        report.status = SceneIOStatus::FileNotFound;
        report.error("file not found");
        return false;
    }

    if (!options.mergeIntoExisting)
        scene->clear();

    const std::filesystem::path baseDir = filePath.parent_path();

    std::string line;
    if (!next_line(in, line))
    {
        report.error("empty file");
        return false;
    }

    int32_t fileVer = 0;
    {
        const auto tok = tokenize(trim(line));
        if (tok.size() != 2 || tok[0] != "imp_scene")
        {
            report.status = SceneIOStatus::UnsupportedFormat;
            report.error("missing 'imp_scene <version>' header");
            return false;
        }
        if (!parse_int32(tok[1], fileVer) || fileVer < 1 || fileVer > 3)
        {
            report.status = SceneIOStatus::UnsupportedFormat;
            report.error("unsupported .imp version");
            return false;
        }
    }

    // imageKey -> ImageId (built during images block, used during materials block)
    std::unordered_map<std::string, ImageId> imageKeyToId;

    while (next_line(in, line))
    {
        const std::string s = trim(line);

        // --------------------------------------------------------
        // images block (v3)
        // --------------------------------------------------------
        if (s == "images" && fileVer >= 3)
        {
            if (!next_line(in, line) || trim(line) != "{")
            {
                report.error("expected '{' after images");
                return false;
            }

            ImageHandler* ih = scene->imageHandler();

            while (next_line(in, line))
            {
                const std::string ms = trim(line);
                if (ms == "}")
                    break;
                if (ms != "image")
                {
                    report.warning(std::string("Unknown images entry: '") + ms + "'");
                    continue;
                }

                ImageBlock ib{};
                if (!parse_image(in, ib, report))
                    return false;

                if (!ih)
                    continue;

                ImageId id = kInvalidImageId;

                if (!ib.path.empty())
                {
                    // External path — resolve relative to scene file
                    const std::filesystem::path full = baseDir / ib.path;
                    id                               = ih->loadFromFile(full, /*flipY=*/true);
                    if (id == kInvalidImageId)
                        report.warning("Could not load image: " + full.string());
                }
                else if (!ib.base64Data.empty())
                {
                    const std::vector<uint8_t> decoded = base64Decode(ib.base64Data);
                    if (!decoded.empty())
                    {
                        if (ib.width > 0 && ib.height > 0 && ib.channels > 0)
                        {
                            // Raw pixels — reconstruct directly. Already flipped on original load
                            // so do NOT flip again here.
                            id = ih->createFromRaw(
                                decoded.data(),
                                ib.width,
                                ib.height,
                                ib.channels,
                                ib.key,
                                /*flipY=*/false);
                            if (id == kInvalidImageId)
                                report.warning("Could not reconstruct raw image: " + ib.key);
                        }
                        else
                        {
                            // Encoded format (KTX etc) — decode via stb/libktx.
                            id = ih->loadFromEncodedMemory(
                                std::span<const unsigned char>(decoded.data(), decoded.size()),
                                ib.key,
                                /*flipY=*/true);
                            if (id == kInvalidImageId)
                                report.warning("Could not decode embedded image: " + ib.key);
                        }
                    }
                }

                if (id != kInvalidImageId)
                    imageKeyToId[ib.key] = id;
            }
            continue;
        }

        // --------------------------------------------------------
        // materials block (v3)
        // --------------------------------------------------------
        if (s == "materials" && fileVer >= 3)
        {
            if (!next_line(in, line) || trim(line) != "{")
            {
                report.error("expected '{' after materials");
                return false;
            }

            MaterialHandler* mh = scene->materialHandler();

            while (next_line(in, line))
            {
                const std::string ms = trim(line);
                if (ms == "}")
                    break;
                if (ms != "material")
                {
                    report.warning(std::string("Unknown materials entry: '") + ms + "'");
                    continue;
                }

                MaterialBlock mb{};
                if (!parse_material(in, mb, report))
                    return false;

                if (!mh)
                    continue;

                const int32_t matId = mh->createMaterial(mb.name);
                Material&     dst   = mh->material(matId);

                dst.alphaMode(static_cast<Material::AlphaMode>(mb.alphaMode));
                dst.doubleSided(mb.doubleSided);
                dst.opacity(mb.opacity);
                dst.roughness(mb.roughness);
                dst.metallic(mb.metallic);
                dst.ior(mb.ior);
                dst.emissiveIntensity(mb.emissiveIntensity);
                dst.baseColor(mb.baseColor);
                dst.emissiveColor(mb.emissiveColor);

                auto resolveTex = [&](const std::string& key) -> ImageId {
                    if (key.empty())
                        return kInvalidImageId;
                    auto it = imageKeyToId.find(key);
                    return (it != imageKeyToId.end()) ? it->second : kInvalidImageId;
                };

                dst.baseColorTexture(resolveTex(mb.baseColorTex));
                dst.normalTexture(resolveTex(mb.normalTex));
                dst.mraoTexture(resolveTex(mb.mraoTex));
                dst.metallicTexture(resolveTex(mb.metallicTex));
                dst.roughnessTexture(resolveTex(mb.roughnessTex));
                dst.aoTexture(resolveTex(mb.aoTex));
                dst.emissiveTexture(resolveTex(mb.emissiveTex));
            }
            continue;
        }

        // --------------------------------------------------------
        // lights block (v3)
        // --------------------------------------------------------
        if (s == "lights" && fileVer >= 3)
        {
            if (!next_line(in, line) || trim(line) != "{")
            {
                report.error("expected '{' after lights");
                return false;
            }

            while (next_line(in, line))
            {
                const std::string ms = trim(line);
                if (ms == "}")
                    break;
                if (ms != "light")
                {
                    report.warning(std::string("Unknown lights entry: '") + ms + "'");
                    continue;
                }

                LightBlock lb{};
                if (!parse_light(in, lb, report))
                    return false;

                Light l{};
                l.name             = lb.name;
                l.type             = static_cast<LightType>(lb.type);
                l.position         = lb.position;
                l.direction        = lb.direction;
                l.color            = lb.color;
                l.intensity        = lb.intensity;
                l.range            = lb.range;
                l.spotInnerConeRad = lb.spotInnerCone;
                l.spotOuterConeRad = lb.spotOuterCone;
                l.enabled          = lb.enabled;
                l.affectRaster     = lb.affectRaster;
                l.affectRt         = lb.affectRt;
                l.castShadows      = lb.castShadows;

                SceneLight* sl = scene->createSceneLight(l);
                if (sl)
                    sl->model(row_major16_to_mat4(lb.modelRM));
                else
                    report.warning("Failed to create light: " + lb.name);
            }
            continue;
        }

        // --------------------------------------------------------
        // mesh block (v1/v2/v3 — unchanged)
        // --------------------------------------------------------
        if (s == "mesh")
        {
            MeshBlock mb{};
            if (!parse_mesh(in, mb, report))
                return false;

            SceneMesh* sm = scene->createSceneMesh(mb.name);
            if (!sm)
            {
                report.error("could not create SceneMesh");
                return false;
            }

            sm->visible(mb.visible);
            sm->selected(mb.selected);
            sm->model(row_major16_to_mat4(mb.modelRM));
            sm->subdivisionLevel(mb.subdivLevel - sm->subdivisionLevel());

            SysMesh* sys = sm->sysMesh();
            if (!sys)
            {
                report.error("null SysMesh");
                return false;
            }

            sys->clear();

            // Reserve from declared vert count for fast bulk creation
            if (!mb.verts.empty())
                sys->reserve(static_cast<int32_t>(mb.verts.size()));

            std::vector<int32_t> newVertIds;
            newVertIds.reserve(mb.verts.size());
            for (const glm::vec3& p : mb.verts)
                newVertIds.push_back(sys->create_vert(p));

            std::vector<int32_t> createdPolyIds;
            createdPolyIds.reserve(mb.polys.size());
            for (const auto& p : mb.polys)
            {
                SysPolyVerts pv;
                for (int32_t di : p.idx)
                {
                    if (di < 0 || di >= static_cast<int32_t>(newVertIds.size()))
                    {
                        report.error("polygon index out of range");
                        return false;
                    }
                    pv.push_back(newVertIds[static_cast<size_t>(di)]);
                }
                if (pv.size() >= 3)
                    createdPolyIds.push_back(sys->create_poly(pv, p.mat));
            }

            for (const MapBindingBlock& m : mb.maps)
            {
                if (m.id < 0 || m.dim <= 0)
                    continue;
                const int32_t existing = sys->map_find(m.id);
                if (existing != -1)
                    sys->map_remove(m.id);
                const int32_t map = sys->map_create(m.id, 0, m.dim);
                if (map < 0)
                {
                    report.warning("Failed to create map id " + std::to_string(m.id));
                    continue;
                }

                std::vector<int32_t> denseToMapVert;
                denseToMapVert.reserve(m.mapVerts.size());
                for (size_t i = 0; i < m.mapVerts.size(); ++i)
                {
                    const auto& vec = m.mapVerts[i];
                    if (static_cast<int32_t>(vec.size()) != m.dim)
                    {
                        std::vector<float> z(static_cast<size_t>(m.dim), 0.f);
                        denseToMapVert.push_back(sys->map_create_vert(map, z.data()));
                        continue;
                    }
                    denseToMapVert.push_back(sys->map_create_vert(map, vec.data()));
                }

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
                    SysPolyVerts mpv;
                    mpv.reserve(pv.size());
                    for (int32_t dmv : b.denseMapVertIndices)
                    {
                        if (dmv < 0 || dmv >= static_cast<int32_t>(denseToMapVert.size()))
                            mpv.push_back(denseToMapVert.empty() ? -1 : denseToMapVert[0]);
                        else
                            mpv.push_back(denseToMapVert[static_cast<size_t>(dmv)]);
                    }
                    sys->map_create_poly(map, polyId, mpv);
                }
            }
            continue;
        }

        report.warning(std::string("Unknown top-level key: '") + s + "'");
    }

    if (report.hasErrors())
        return false;
    report.status = SceneIOStatus::Ok;
    report.info("Loaded .imp v" + std::to_string(fileVer) + " scene");
    return true;
}
