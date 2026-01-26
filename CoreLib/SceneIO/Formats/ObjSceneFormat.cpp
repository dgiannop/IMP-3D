#include "ObjSceneFormat.hpp"

#include <SysMesh.hpp>
#include <SysObjLoader.hpp>
#include <filesystem>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SceneMtlUtils.hpp"

bool ObjSceneFormat::load(Scene*                       scene,
                          const std::filesystem::path& filePath,
                          const LoadOptions&           options,
                          SceneIOReport&               report)
{
    // ---------------------------------------------------------
    // Basic validation
    // ---------------------------------------------------------
    if (scene == nullptr)
    {
        report.error("SceneFormatOBJ::load: scene is null");
        report.status = SceneIOStatus::InvalidScene; // adjust if you don't have this enum
        return false;
    }

    if (!std::filesystem::exists(filePath))
    {
        report.status = SceneIOStatus::FileNotFound;
        report.error("File not found: " + filePath.string());
        return false;
    }

    // ---------------------------------------------------------
    // Merge / replace behavior
    // ---------------------------------------------------------
    if (!options.mergeIntoExisting)
    {
        scene->clear();
    }

    // ---------------------------------------------------------
    // Open file
    // ---------------------------------------------------------
    std::ifstream file(filePath);
    if (!file)
    {
        report.status = SceneIOStatus::FileNotFound;
        report.error("Failed to open OBJ file: " + filePath.string());
        return false;
    }

    MaterialHandler* materials = scene->materialHandler();
    if (!materials)
    {
        report.status = SceneIOStatus::InvalidScene;
        report.error("SceneFormatOBJ::load: scene->materials() returned null");
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    std::string line;
    std::string matlib;

    std::uint32_t matIndex        = 0;
    int           normMap         = -1;
    int           texMap          = -1;
    bool          seenFirstObject = false;

    // Fallback mesh in case "o" never appears
    SceneMesh* sceneMesh   = scene->createSceneMesh("Default");
    SysMesh*   currentMesh = sceneMesh->sysMesh();
    normMap                = currentMesh->map_create(0, 0, 3);
    texMap                 = currentMesh->map_create(1, 0, 2);

    std::unordered_map<int, int> globalToLocal;

    auto startNewMesh = [&](const std::string& name) {
        sceneMesh   = scene->createSceneMesh(name);
        currentMesh = sceneMesh->sysMesh();
        normMap     = currentMesh->map_create(0, 0, 3);
        texMap      = currentMesh->map_create(1, 0, 2);
        globalToLocal.clear();
        seenFirstObject = true;
    };

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string        prefix;
        iss >> prefix;

        if (prefix == "mtllib")
        {
            // Allow spaces in mtllib
            std::getline(iss >> std::ws, matlib);

            // trim trailing CR (Windows line endings)
            if (!matlib.empty() && matlib.back() == '\r')
                matlib.pop_back();
            // iss >> matlib; //old
        }
        else if (prefix == "usemtl")
        {
            std::string matName;
            iss >> matName;
            matIndex = materials->createMaterial(matName);
        }
        else if (prefix == "o")
        {
            std::string objName;
            iss >> objName;
            startNewMesh(objName);
        }
        else if (prefix == "v")
        {
            glm::vec3 v{};
            iss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (prefix == "vn")
        {
            glm::vec3 n{};
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (prefix == "vt")
        {
            glm::vec2 uv{};
            iss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        }
        else if (prefix == "f")
        {
            // if (!seenFirstObject && sceneMesh->name() == "Default")
            // {
            //     // Optional: warning if no "o" blocks
            //     // report.warning("OBJ file does not use 'o' object blocks. Geometry will be loaded into 'Default'.");
            // }

            struct Idx
            {
                int v = -1;
                int t = -1;
                int n = -1;
            };

            std::vector<Idx> face;
            std::string      token;

            while (iss >> token)
            {
                Idx    idx{};
                size_t s1 = token.find('/');
                size_t s2 = (s1 == std::string::npos) ? std::string::npos : token.find('/', s1 + 1);

                auto parse_index = [](const std::string& str, int size) -> int {
                    if (str.empty())
                        return -1;
                    int idx = std::stoi(str);
                    if (idx > 0)
                        return idx - 1;
                    if (idx < 0)
                        return size + idx;
                    return -1;
                };

                if (s1 == std::string::npos)
                {
                    idx.v = parse_index(token, static_cast<int>(positions.size()));
                }
                else
                {
                    idx.v = parse_index(token.substr(0, s1), static_cast<int>(positions.size()));
                    if (s2 == std::string::npos && s1 + 1 < token.size())
                    {
                        idx.t = parse_index(token.substr(s1 + 1), static_cast<int>(texcoords.size()));
                    }
                    else
                    {
                        if (s1 + 1 < s2)
                        {
                            idx.t = parse_index(
                                token.substr(s1 + 1, s2 - s1 - 1),
                                static_cast<int>(texcoords.size()));
                        }
                        if (s2 + 1 < token.size())
                        {
                            idx.n = parse_index(token.substr(s2 + 1), static_cast<int>(normals.size()));
                        }
                    }
                }

                face.push_back(idx);
            }

            SysPolyVerts pv;
            SysPolyVerts pn;
            SysPolyVerts pt;

            for (const Idx& idx : face)
            {
                if (idx.v < 0 || idx.v >= static_cast<int>(positions.size()))
                {
                    report.error("Invalid vertex index in face, skipping polygon.");
                    pv.clear();
                    break;
                }

                const int posKey  = idx.v;
                int       meshIdx = -1;

                auto it = globalToLocal.find(posKey);
                if (it != globalToLocal.end())
                {
                    meshIdx = it->second;
                }
                else
                {
                    meshIdx               = currentMesh->create_vert(positions[idx.v]);
                    globalToLocal[posKey] = meshIdx;
                }

                pv.push_back(meshIdx);

                if (normMap != -1 && idx.n >= 0 &&
                    idx.n < static_cast<int>(normals.size()))
                {
                    pn.push_back(currentMesh->map_create_vert(normMap, glm::value_ptr(normals[idx.n])));
                }

                if (texMap != -1 && idx.t >= 0 &&
                    idx.t < static_cast<int>(texcoords.size()))
                {
                    pt.push_back(currentMesh->map_create_vert(texMap, glm::value_ptr(texcoords[idx.t])));
                }
            }

            if (pv.size() >= 3)
            {
                const int poly = currentMesh->create_poly(pv, matIndex);
                if (pn.size() == pv.size())
                {
                    currentMesh->map_create_poly(normMap, poly, pn);
                }
                if (pt.size() == pv.size())
                {
                    currentMesh->map_create_poly(texMap, poly, pt);
                }
            }
        }
    }

    file.close();

    // ---------------------------------------------------------
    // Load .mtl if present
    // ---------------------------------------------------------
    if (!matlib.empty())
    {
        const std::filesystem::path mtlPath = filePath.parent_path() / matlib;
        loadMaterialLibrary(scene, mtlPath);
        // If loadMaterialLibrary can fail and return bool, you can hook it into report here.
    }

    if (report.status == SceneIOStatus::Ok)
    {
        // Optionally add info message
        // report.info("Successfully loaded OBJ: " + filePath.string());
    }

    for (auto m : scene->meshes())
    {
        std::cerr << "Mesh loaded ------------------------------\n";
        std::cerr << "Num verts: = " << m->num_verts() << std::endl;
        std::cerr << "Num polys: = " << m->num_polys() << std::endl;
        std::cerr << "------------------------------------------\n";
    }
    return true;
}

bool ObjSceneFormat::loadMaterialLibrary(Scene* scene, const std::filesystem::path& filePath)
{
    std::ifstream file(filePath);
    if (!file)
    {
        // Add this to SceneIOReport later
        // std::cerr << "Could not open material file: " << filePath << "\n";
        return false;
    }

    MaterialHandler* materialHandler = scene->materialHandler();
    if (materialHandler == nullptr)
        return false;

    int         currentIndex = -1;
    std::string line;
    MtlFields   cur{}; // assuming default-constructible

    auto commit = [&]() {
        if (currentIndex < 0)
            return;

        Material& dst = materialHandler->material(currentIndex);
        // Fill PBR + textures from parsed MTL fields
        fromMTL(scene, dst, cur, filePath);
    };

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string        kw;
        iss >> kw;

        if (kw == "newmtl")
        {
            // Finish previous material
            commit();

            std::string name;
            iss >> name;

            // Create or re-use by name (your handler enforces uniqueness + defaults)
            currentIndex = materialHandler->createMaterial(name);

            cur      = MtlFields{};
            cur.name = name;
        }
        else if (currentIndex >= 0)
        {
            if (kw == "Kd")
                iss >> cur.Kd.r >> cur.Kd.g >> cur.Kd.b;
            else if (kw == "Ka")
                iss >> cur.Ka.r >> cur.Ka.g >> cur.Ka.b;
            else if (kw == "Ks")
                iss >> cur.Ks.r >> cur.Ks.g >> cur.Ks.b;
            else if (kw == "Ke")
                iss >> cur.Ke.r >> cur.Ke.g >> cur.Ke.b;
            else if (kw == "Tf")
                iss >> cur.Tf.r >> cur.Tf.g >> cur.Tf.b;
            else if (kw == "Tr")
                iss >> cur.Tr;
            else if (kw == "Ns")
                iss >> cur.Ns;
            else if (kw == "Ni")
                iss >> cur.Ni;
            else if (kw == "d")
                iss >> cur.d;
            else if (kw == "map_Kd")
                std::getline(iss >> std::ws, cur.map_Kd);
            else if (kw == "map_bump")
                std::getline(iss >> std::ws, cur.map_bump);
            else if (kw == "map_Ke")
                std::getline(iss >> std::ws, cur.map_Ke);
            else if (kw == "map_Ks")
                std::getline(iss >> std::ws, cur.map_Ks);
            else if (kw == "map_Ka")
                std::getline(iss >> std::ws, cur.map_Ka);
            else if (kw == "map_Tr")
                std::getline(iss >> std::ws, cur.map_Tr);
        }
    }

    // Commit the last material
    commit();
    return true;
}

bool ObjSceneFormat::save(const Scene*                 scene,
                          const std::filesystem::path& filePath,
                          const SaveOptions&           options,
                          SceneIOReport&               report)
{
    if (scene == nullptr)
    {
        report.error("SceneFormatOBJ::save: scene is null");
        report.status = SceneIOStatus::InvalidScene;
        return false;
    }

    // ---------------------------------------------------------------------
    // Open OBJ file
    // ---------------------------------------------------------------------
    std::ofstream out(filePath);
    if (!out)
    {
        std::string msg = "SceneFormatOBJ::save: failed to open OBJ file for writing: " + filePath.string();
        report.error(msg);
        report.status = SceneIOStatus::WriteError;
        return false;
    }

    // ---------------------------------------------------------------------
    // MTL file name/path
    // ---------------------------------------------------------------------
    std::string           mtlFilename = filePath.filename().replace_extension(".mtl").string();
    std::filesystem::path mtlPath     = filePath.parent_path() / mtlFilename;

    out << "mtllib " << mtlFilename << "\n";

    // ---------------------------------------------------------------------
    // Material access
    // ---------------------------------------------------------------------
    const MaterialHandler* materialHandler = scene->materialHandler();
    if (!materialHandler)
    {
        report.error("SceneFormatOBJ::save: scene->materialHandler() returned null");
        report.status = SceneIOStatus::InvalidScene;
        return false;
    }

    const auto& materials = materialHandler->materials(); // vector<Material or SceneMaterial>

    // ---------------------------------------------------------------------
    // Iterate over scene meshes
    // ---------------------------------------------------------------------
    int vertBase = 1;
    int normBase = 1;
    int texBase  = 1;

    int unnamedCounter = 1;

    const auto& sceneMeshes = scene->sceneMeshes(); // whatever container of SceneMesh*

    for (const SceneMesh* sm : sceneMeshes)
    {
        if (!sm)
            continue;

        // If you later want "selectedOnly", you can check here:
        // if (options.selectedOnly && !sMesh->isSelected()) continue;

        const SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        std::string name = std::string(sm->name());
        if (name.empty() || name == "Unnamed")
            name = "Unnamed_" + std::to_string(unnamedCounter++);

        out << "# OriginalName: " << name << "\n";
        out << "o " << sanitizeName(name) << "\n";

        const int normalMap = mesh->map_find(0); // consistent with load(): 0 = normal map
        const int texMap    = mesh->map_find(1); // 1 = texcoord map

        // -----------------------------------------------------------------
        // Write vertex positions (v)
        // -----------------------------------------------------------------
        for (int vi : mesh->all_verts())
        {
            const glm::vec3 pos = mesh->vert_position(vi);
            out << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
        }

        // -----------------------------------------------------------------
        // Collect used face-varying map-verts (normals / texcoords)
        // -----------------------------------------------------------------
        std::vector<int32_t> usedNormIds;
        usedNormIds.reserve(1024);
        std::vector<int32_t> usedTexIds;
        usedTexIds.reserve(1024);

        std::unordered_set<int32_t> seenN;
        std::unordered_set<int32_t> seenT;

        if (normalMap != -1 || texMap != -1)
        {
            for (int pi : mesh->all_polys())
            {
                if (normalMap != -1)
                {
                    const auto& pn = mesh->map_poly_verts(normalMap, pi);
                    for (int id : pn)
                    {
                        if (id >= 0 && !seenN.count(id))
                        {
                            seenN.insert(id);
                            usedNormIds.push_back(id);
                        }
                    }
                }
                if (texMap != -1)
                {
                    const auto& pt = mesh->map_poly_verts(texMap, pi);
                    for (int id : pt)
                    {
                        if (id >= 0 && !seenT.count(id))
                        {
                            seenT.insert(id);
                            usedTexIds.push_back(id);
                        }
                    }
                }
            }
        }

        // -----------------------------------------------------------------
        // Emit vt / vn in face-varying domain and build remaps
        // mapVertId -> OBJ index (1-based)
        // -----------------------------------------------------------------
        std::unordered_map<int32_t, int32_t> texcoordRemap; // mapVertId -> vt index
        std::unordered_map<int32_t, int32_t> normalRemap;   // mapVertId -> vn index

        // vt
        if (texMap != -1)
        {
            int written = 0;
            for (int id : usedTexIds)
            {
                const glm::vec2 uv = glm::make_vec2(mesh->map_vert_position(texMap, id));
                texcoordRemap[id]  = texBase + written;
                out << "vt " << uv.x << " " << uv.y << "\n";
                ++written;
            }
            texBase += written;
        }

        // vn
        if (normalMap != -1)
        {
            int written = 0;
            for (int id : usedNormIds)
            {
                const glm::vec3 n = glm::make_vec3(mesh->map_vert_position(normalMap, id));
                normalRemap[id]   = normBase + written;
                out << "vn " << n.x << " " << n.y << " " << n.z << "\n";
                ++written;
            }
            normBase += written;
        }

        // -----------------------------------------------------------------
        // Group polygons by material and write faces
        // -----------------------------------------------------------------
        std::map<int, std::vector<int>> polysByMat;
        for (int pi : mesh->all_polys())
        {
            const int matIndex = mesh->poly_material(pi);
            polysByMat[matIndex].push_back(pi);
        }

        for (const auto& [matIndex, polyList] : polysByMat)
        {
            const std::string matName =
                (matIndex >= 0 && matIndex < static_cast<int>(materials.size()))
                    ? materials[matIndex].name()
                    : std::string("Default");

            out << "usemtl " << sanitizeName(matName) << "\n";

            for (int pi : polyList)
            {
                const auto& verts = mesh->poly_verts(pi);
                const auto& pn    = (normalMap != -1) ? mesh->map_poly_verts(normalMap, pi) : SysPolyVerts{};
                const auto& pt    = (texMap != -1) ? mesh->map_poly_verts(texMap, pi) : SysPolyVerts{};

                const bool hasN  = (normalMap != -1) && (pn.size() == verts.size());
                const bool hasUV = (texMap != -1) && (pt.size() == verts.size());

                out << "f";
                for (size_t i = 0; i < verts.size(); ++i)
                {
                    const int vIdx = vertBase + verts[i]; // OBJ vertex index (1-based)

                    if (hasUV && hasN)
                    {
                        const int vtIdx = texcoordRemap.at(pt[i]);
                        const int vnIdx = normalRemap.at(pn[i]);
                        out << " " << vIdx << "/" << vtIdx << "/" << vnIdx;
                    }
                    else if (hasUV)
                    {
                        const int vtIdx = texcoordRemap.at(pt[i]);
                        out << " " << vIdx << "/" << vtIdx;
                    }
                    else if (hasN)
                    {
                        const int vnIdx = normalRemap.at(pn[i]);
                        out << " " << vIdx << "//" << vnIdx;
                    }
                    else
                    {
                        out << " " << vIdx;
                    }
                }
                out << "\n";
            }
        }

        // Bump vertex base for next object
        vertBase += mesh->num_verts();
    }

    out.close();
    if (!out)
    {
        report.error("SceneFormatOBJ::save: write error while closing OBJ file");
        report.status = SceneIOStatus::WriteError;
        return false;
    }

    // ---------------------------------------------------------------------
    // Save MTL library next to OBJ
    // ---------------------------------------------------------------------
    if (!saveMaterialLibrary(scene, mtlPath))
    {
        // We keep OBJ as "saved" but flag that MTL failed.
        if (report.status == SceneIOStatus::Ok)
            report.status = SceneIOStatus::WriteError;

        report.error("SceneFormatOBJ::save: failed to write MTL file: " + mtlPath.string());
        return false; // or 'true' if you want OBJ-only to count as success
    }

    if (report.status == SceneIOStatus::Ok)
        report.status = SceneIOStatus::Ok; // explicitly

    return true;
}

bool ObjSceneFormat::saveMaterialLibrary(const Scene* scene, const std::filesystem::path& filePath)
{
    const auto&   materials = scene->materialHandler()->materials(); // vector<SceneMaterial>
    std::ofstream out(filePath);
    if (!out)
    {
        std::cerr << "Could not open material file for writing: " << filePath << "\n";
        return false;
    }

    out << std::fixed << std::setprecision(6);

    for (const auto& mat : materials)
    {
        const MtlFields mtl = toMTL(mat, scene);

        out << "newmtl " << sanitizeName(mat.name()) << "\n";
        out << "Ka " << mtl.Ka.r << " " << mtl.Ka.g << " " << mtl.Ka.b << "\n";
        out << "Kd " << mtl.Kd.r << " " << mtl.Kd.g << " " << mtl.Kd.b << "\n";
        out << "Ks " << mtl.Ks.r << " " << mtl.Ks.g << " " << mtl.Ks.b << "\n";
        out << "Ke " << mtl.Ke.r << " " << mtl.Ke.g << " " << mtl.Ke.b << "\n";
        out << "Tf " << mtl.Tf.r << " " << mtl.Tf.g << " " << mtl.Tf.b << "\n";
        out << "Tr " << mtl.Tr << "\n";
        out << "Ns " << mtl.Ns << "\n";
        out << "Ni " << mtl.Ni << "\n";
        out << "d " << mtl.d << "\n";

        // Relative, sanitized paths
        const auto dir = filePath.parent_path();
        if (!mtl.map_Kd.empty())
            out << "map_Kd " << PathUtil::relativeSanitized(mtl.map_Kd, dir) << "\n";
        if (!mtl.map_bump.empty())
            out << "map_bump " << PathUtil::relativeSanitized(mtl.map_bump, dir) << "\n";
        if (!mtl.map_Ke.empty())
            out << "map_Ke " << PathUtil::relativeSanitized(mtl.map_Ke, dir) << "\n";
        // Optional: map_Ks/map_Ka/map_Tr

        out << "\n";
    }
    return true;
}
