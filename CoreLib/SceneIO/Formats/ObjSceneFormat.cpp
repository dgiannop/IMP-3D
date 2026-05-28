// #if 0

// #else

// #include "ObjSceneFormat.hpp"

// #include <SysMesh.hpp>
// #include <SysObjLoader.hpp>
// #include <cctype>
// #include <cstdlib>
// #include <cstring>
// #include <filesystem>
// #include <fstream>
// #include <glm/gtc/type_ptr.hpp>
// #include <iostream>
// #include <map>
// #include <sstream>
// #include <string>
// #include <unordered_map>
// #include <unordered_set>
// #include <vector>

// #include "CoreUtilities.hpp"
// #include "Scene.hpp"
// #include "SceneMesh.hpp"
// #include "SceneMtlUtils.hpp"

// // ------------------------------------------------------------
// // OBJ has no header counts. These are only fallback reserves.
// // The loader below reads the whole file into memory and parses
// // with char pointers instead of iostreams/istringstream/stoi.
// // ------------------------------------------------------------
// static constexpr int32_t kObjReserveVerts = 2048;

// namespace
// {
//     inline bool is_space(char c)
//     {
//         return c == ' ' || c == '\t' || c == '\r' || c == '\n';
//     }

//     inline const char* skip_spaces(const char* p, const char* e)
//     {
//         while (p < e && (*p == ' ' || *p == '\t'))
//             ++p;
//         return p;
//     }

//     inline const char* skip_to_line_end(const char* p, const char* e)
//     {
//         while (p < e && *p != '\n')
//             ++p;
//         return p;
//     }

//     inline const char* next_line(const char* p, const char* e)
//     {
//         p = skip_to_line_end(p, e);
//         if (p < e && *p == '\n')
//             ++p;
//         return p;
//     }

//     inline std::string read_token_string(const char*& p, const char* e)
//     {
//         p             = skip_spaces(p, e);
//         const char* b = p;
//         while (p < e && !is_space(*p))
//             ++p;
//         return std::string(b, p);
//     }

//     inline std::string read_rest_of_line_trimmed(const char*& p, const char* e)
//     {
//         p             = skip_spaces(p, e);
//         const char* b = p;
//         const char* q = p;
//         while (q < e && *q != '\n' && *q != '\r')
//             ++q;
//         const char* r = q;
//         while (r > b && is_space(*(r - 1)))
//             --r;
//         p = q;
//         return std::string(b, r);
//     }

//     inline bool parse_float3(const char*& p, const char* e, glm::vec3& v)
//     {
//         p         = skip_spaces(p, e);
//         char* out = nullptr;
//         v.x       = std::strtof(p, &out);
//         if (out == p)
//             return false;
//         p   = skip_spaces(out, e);
//         v.y = std::strtof(p, &out);
//         if (out == p)
//             return false;
//         p   = skip_spaces(out, e);
//         v.z = std::strtof(p, &out);
//         if (out == p)
//             return false;
//         p = out;
//         return true;
//     }

//     inline bool parse_float2(const char*& p, const char* e, glm::vec2& v)
//     {
//         p         = skip_spaces(p, e);
//         char* out = nullptr;
//         v.x       = std::strtof(p, &out);
//         if (out == p)
//             return false;
//         p   = skip_spaces(out, e);
//         v.y = std::strtof(p, &out);
//         if (out == p)
//             return false;
//         p = out;
//         return true;
//     }

//     inline int resolve_obj_index(int idx, int size)
//     {
//         if (idx > 0)
//             return idx - 1;
//         if (idx < 0)
//             return size + idx;
//         return -1;
//     }

//     struct ObjIdx
//     {
//         int v = -1;
//         int t = -1;
//         int n = -1;
//     };

//     // Parses one OBJ face vertex token:
//     //   v
//     //   v/t
//     //   v//n
//     //   v/t/n
//     // No allocation, no substr, no stoi.
//     inline ObjIdx parse_face_vertex(const char*& p,
//                                     const char*  e,
//                                     int          posCount,
//                                     int          texCount,
//                                     int          normCount)
//     {
//         ObjIdx idx{};

//         char*      out  = nullptr;
//         const long vRaw = std::strtol(p, &out, 10);
//         if (out == p)
//             return idx;

//         idx.v = resolve_obj_index(static_cast<int>(vRaw), posCount);
//         p     = out;

//         if (p >= e || *p != '/')
//             return idx;

//         ++p; // first slash

//         // v//n
//         if (p < e && *p == '/')
//         {
//             ++p; // second slash
//             const long nRaw = std::strtol(p, &out, 10);
//             if (out != p)
//             {
//                 idx.n = resolve_obj_index(static_cast<int>(nRaw), normCount);
//                 p     = out;
//             }
//             return idx;
//         }

//         // v/t or v/t/n
//         const long tRaw = std::strtol(p, &out, 10);
//         if (out != p)
//         {
//             idx.t = resolve_obj_index(static_cast<int>(tRaw), texCount);
//             p     = out;
//         }

//         if (p < e && *p == '/')
//         {
//             ++p;
//             const long nRaw = std::strtol(p, &out, 10);
//             if (out != p)
//             {
//                 idx.n = resolve_obj_index(static_cast<int>(nRaw), normCount);
//                 p     = out;
//             }
//         }

//         return idx;
//     }

//     inline void ensure_vector_size(std::vector<int>& v, int index)
//     {
//         if (index >= static_cast<int>(v.size()))
//             v.resize(static_cast<size_t>(index) + 1, -1);
//     }
// } // namespace

// bool ObjSceneFormat::load(Scene*                       scene,
//                           const std::filesystem::path& filePath,
//                           const LoadOptions&           options,
//                           SceneIOReport&               report)
// {
//     TICK(OBJ_LOAD);

//     // ---------------------------------------------------------
//     // Basic validation
//     // ---------------------------------------------------------
//     if (scene == nullptr)
//     {
//         report.error("SceneFormatOBJ::load: scene is null");
//         report.status = SceneIOStatus::InvalidScene;
//         return false;
//     }

//     if (!std::filesystem::exists(filePath))
//     {
//         report.status = SceneIOStatus::FileNotFound;
//         report.error("File not found: " + filePath.string());
//         return false;
//     }

//     // ---------------------------------------------------------
//     // Merge / replace behavior
//     // ---------------------------------------------------------
//     if (!options.mergeIntoExisting)
//     {
//         scene->clear();
//     }

//     // ---------------------------------------------------------
//     // Read the whole OBJ file into memory.
//     // This is usually much faster than std::getline + istringstream.
//     // ---------------------------------------------------------
//     std::ifstream file(filePath, std::ios::binary | std::ios::ate);
//     if (!file)
//     {
//         report.status = SceneIOStatus::FileNotFound;
//         report.error("Failed to open OBJ file: " + filePath.string());
//         return false;
//     }

//     const std::streamsize fileSize = file.tellg();
//     if (fileSize < 0)
//     {
//         report.status = SceneIOStatus::ReadError;
//         report.error("Failed to determine OBJ file size: " + filePath.string());
//         return false;
//     }

//     std::string buffer;
//     buffer.resize(static_cast<size_t>(fileSize));
//     file.seekg(0, std::ios::beg);
//     if (fileSize > 0 && !file.read(buffer.data(), fileSize))
//     {
//         report.status = SceneIOStatus::ReadError;
//         report.error("Failed to read OBJ file: " + filePath.string());
//         return false;
//     }
//     file.close();

//     MaterialHandler* materials = scene->materialHandler();
//     if (!materials)
//     {
//         report.status = SceneIOStatus::InvalidScene;
//         report.error("SceneFormatOBJ::load: scene->materials() returned null");
//         return false;
//     }

//     std::vector<glm::vec3> positions;
//     std::vector<glm::vec3> normals;
//     std::vector<glm::vec2> texcoords;

//     positions.reserve(kObjReserveVerts);
//     normals.reserve(kObjReserveVerts);
//     texcoords.reserve(kObjReserveVerts);

//     std::string matlib;

//     std::uint32_t matIndex = 0;
//     int           normMap  = -1;
//     int           texMap   = -1;

//     SceneMesh* sceneMesh   = nullptr;
//     SysMesh*   currentMesh = nullptr;

//     // OBJ vertex position index -> current SysMesh vertex index.
//     // Vector lookup is much faster than unordered_map for dense OBJ indices.
//     std::vector<int> globalToLocal;

//     // Normals/UVs are intentionally stored as face-corner map verts in SysMesh.
//     // Do not cache/reuse OBJ vn/vt indices here; one OBJ face corner becomes
//     // one SysMesh map vert so edit behavior remains identical to the original loader.

//     auto resetMeshLocalCaches = [&]() {
//         globalToLocal.clear();
//     };

//     auto ensureDefaultMesh = [&]() {
//         if (currentMesh)
//             return;

//         sceneMesh   = scene->createSceneMesh("Default");
//         currentMesh = sceneMesh->sysMesh();
//         currentMesh->reserve(kObjReserveVerts);
//         normMap = currentMesh->map_create(/*MESH_MAP_NORMALS*/ 0, 0, 3);
//         texMap  = currentMesh->map_create(/*MESH_MAP_UV0*/ 1, 0, 2);
//         resetMeshLocalCaches();
//     };

//     auto startNewMesh = [&](const std::string& name) {
//         sceneMesh   = scene->createSceneMesh(name.empty() ? std::string("Object") : name);
//         currentMesh = sceneMesh->sysMesh();
//         currentMesh->reserve(kObjReserveVerts);
//         normMap = currentMesh->map_create(/*MESH_MAP_NORMALS*/ 0, 0, 3);
//         texMap  = currentMesh->map_create(/*MESH_MAP_UV0*/ 1, 0, 2);
//         resetMeshLocalCaches();
//     };

//     const char* p = buffer.data();
//     const char* e = buffer.data() + buffer.size();

//     while (p < e)
//     {
//         p = skip_spaces(p, e);
//         if (p >= e)
//             break;

//         if (*p == '#')
//         {
//             p = next_line(p, e);
//             continue;
//         }

//         // -----------------------------------------------------
//         // Vertex attributes
//         // -----------------------------------------------------
//         if (*p == 'v')
//         {
//             if (p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
//             {
//                 p += 2;
//                 glm::vec3 v{};
//                 if (parse_float3(p, e, v))
//                     positions.push_back(v);
//                 p = next_line(p, e);
//                 continue;
//             }

//             if (p + 2 < e && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t'))
//             {
//                 p += 3;
//                 glm::vec3 n{};
//                 if (parse_float3(p, e, n))
//                     normals.push_back(n);
//                 p = next_line(p, e);
//                 continue;
//             }

//             if (p + 2 < e && p[1] == 't' && (p[2] == ' ' || p[2] == '\t'))
//             {
//                 p += 3;
//                 glm::vec2 uv{};
//                 if (parse_float2(p, e, uv))
//                     texcoords.push_back(uv);
//                 p = next_line(p, e);
//                 continue;
//             }
//         }

//         // -----------------------------------------------------
//         // Face
//         // -----------------------------------------------------
//         if (*p == 'f' && p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
//         {
//             ensureDefaultMesh();
//             p += 2;

//             // Most OBJ faces are triangles/quads. SmallList handles inline storage,
//             // so these should not heap allocate for triangles/quads.
//             SysPolyVerts pv;
//             SysPolyVerts pn;
//             SysPolyVerts pt;

//             bool invalidFace = false;

//             while (p < e && *p != '\n' && *p != '\r')
//             {
//                 p = skip_spaces(p, e);
//                 if (p >= e || *p == '\n' || *p == '\r')
//                     break;

//                 const ObjIdx idx = parse_face_vertex(
//                     p,
//                     e,
//                     static_cast<int>(positions.size()),
//                     static_cast<int>(texcoords.size()),
//                     static_cast<int>(normals.size()));

//                 if (idx.v < 0 || idx.v >= static_cast<int>(positions.size()))
//                 {
//                     invalidFace = true;
//                     break;
//                 }

//                 ensure_vector_size(globalToLocal, idx.v);

//                 int& meshIdx = globalToLocal[idx.v];
//                 if (meshIdx == -1)
//                 {
//                     meshIdx = currentMesh->create_vert(positions[idx.v]);
//                 }
//                 pv.push_back(meshIdx);

//                 if (normMap != -1 && idx.n >= 0 && idx.n < static_cast<int>(normals.size()))
//                 {
//                     pn.push_back(currentMesh->map_create_vert(normMap, glm::value_ptr(normals[idx.n])));
//                 }

//                 if (texMap != -1 && idx.t >= 0 && idx.t < static_cast<int>(texcoords.size()))
//                 {
//                     pt.push_back(currentMesh->map_create_vert(texMap, glm::value_ptr(texcoords[idx.t])));
//                 }
//             }

//             if (!invalidFace && pv.size() >= 3)
//             {
//                 const int poly = currentMesh->create_poly(pv, matIndex);
//                 if (pn.size() == pv.size())
//                     currentMesh->map_create_poly(normMap, poly, pn);
//                 if (pt.size() == pv.size())
//                     currentMesh->map_create_poly(texMap, poly, pt);
//             }
//             else if (invalidFace)
//             {
//                 report.error("Invalid vertex index in face, skipping polygon.");
//             }

//             p = next_line(p, e);
//             continue;
//         }

//         // -----------------------------------------------------
//         // Object name
//         // -----------------------------------------------------
//         if (*p == 'o' && p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
//         {
//             p += 2;
//             std::string objName = read_token_string(p, e);
//             startNewMesh(objName);
//             p = next_line(p, e);
//             continue;
//         }

//         // -----------------------------------------------------
//         // Material library
//         // -----------------------------------------------------
//         if ((e - p) >= 6 && std::memcmp(p, "mtllib", 6) == 0 && (p + 6 == e || is_space(p[6])))
//         {
//             p += 6;
//             matlib = read_rest_of_line_trimmed(p, e);
//             p      = next_line(p, e);
//             continue;
//         }

//         // -----------------------------------------------------
//         // Material assignment
//         // -----------------------------------------------------
//         if ((e - p) >= 6 && std::memcmp(p, "usemtl", 6) == 0 && (p + 6 == e || is_space(p[6])))
//         {
//             p += 6;
//             std::string matName = read_token_string(p, e);
//             matIndex            = materials->createMaterial(matName);
//             p                   = next_line(p, e);
//             continue;
//         }

//         // Ignore unsupported OBJ commands: g, s, l, p, vp, etc.
//         p = next_line(p, e);
//     }

//     // ---------------------------------------------------------
//     // Load .mtl if present
//     // ---------------------------------------------------------
//     if (!matlib.empty())
//     {
//         const std::filesystem::path mtlPath = filePath.parent_path() / matlib;
//         loadMaterialLibrary(scene, mtlPath);
//     }

//     TOCK(OBJ_LOAD);

//     for (auto m : scene->meshes())
//     {
//         std::cerr << "Mesh loaded ------------------------------\n";
//         std::cerr << "Num verts: = " << m->num_verts() << std::endl;
//         std::cerr << "Num polys: = " << m->num_polys() << std::endl;
//         std::cerr << "------------------------------------------\n";
//     }

//     return true;
// }

// bool ObjSceneFormat::loadMaterialLibrary(Scene* scene, const std::filesystem::path& filePath)
// {
//     std::ifstream file(filePath);
//     if (!file)
//         return false;

//     MaterialHandler* materialHandler = scene->materialHandler();
//     if (materialHandler == nullptr)
//         return false;

//     int         currentIndex = -1;
//     std::string line;
//     MtlFields   cur{};

//     auto commit = [&]() {
//         if (currentIndex < 0)
//             return;
//         Material& dst = materialHandler->material(currentIndex);
//         fromMTL(scene, dst, cur, filePath);
//     };

//     while (std::getline(file, line))
//     {
//         if (line.empty() || line[0] == '#')
//             continue;

//         std::istringstream iss(line);
//         std::string        kw;
//         iss >> kw;

//         if (kw == "newmtl")
//         {
//             commit();

//             std::string name;
//             iss >> name;

//             currentIndex = materialHandler->createMaterial(name);
//             cur          = MtlFields{};
//             cur.name     = name;
//         }
//         else if (currentIndex >= 0)
//         {
//             if (kw == "Kd")
//                 iss >> cur.Kd.r >> cur.Kd.g >> cur.Kd.b;
//             else if (kw == "Ka")
//                 iss >> cur.Ka.r >> cur.Ka.g >> cur.Ka.b;
//             else if (kw == "Ks")
//                 iss >> cur.Ks.r >> cur.Ks.g >> cur.Ks.b;
//             else if (kw == "Ke")
//                 iss >> cur.Ke.r >> cur.Ke.g >> cur.Ke.b;
//             else if (kw == "Tf")
//                 iss >> cur.Tf.r >> cur.Tf.g >> cur.Tf.b;
//             else if (kw == "Tr")
//                 iss >> cur.Tr;
//             else if (kw == "Ns")
//                 iss >> cur.Ns;
//             else if (kw == "Ni")
//                 iss >> cur.Ni;
//             else if (kw == "d")
//                 iss >> cur.d;
//             else if (kw == "map_Kd")
//                 std::getline(iss >> std::ws, cur.map_Kd);
//             else if (kw == "map_bump")
//                 std::getline(iss >> std::ws, cur.map_bump);
//             else if (kw == "map_Ke")
//                 std::getline(iss >> std::ws, cur.map_Ke);
//             else if (kw == "map_Ks")
//                 std::getline(iss >> std::ws, cur.map_Ks);
//             else if (kw == "map_Ka")
//                 std::getline(iss >> std::ws, cur.map_Ka);
//             else if (kw == "map_Tr")
//                 std::getline(iss >> std::ws, cur.map_Tr);
//         }
//     }

//     commit();
//     return true;
// }

// bool ObjSceneFormat::save(const Scene*                 scene,
//                           const std::filesystem::path& filePath,
//                           const SaveOptions&           options,
//                           SceneIOReport&               report)
// {
//     if (scene == nullptr)
//     {
//         report.error("SceneFormatOBJ::save: scene is null");
//         report.status = SceneIOStatus::InvalidScene;
//         return false;
//     }

//     // ---------------------------------------------------------------------
//     // Open OBJ file
//     // ---------------------------------------------------------------------
//     std::ofstream out(filePath);
//     if (!out)
//     {
//         report.error("SceneFormatOBJ::save: failed to open OBJ file for writing: " + filePath.string());
//         report.status = SceneIOStatus::WriteError;
//         return false;
//     }

//     // ---------------------------------------------------------------------
//     // MTL file name/path
//     // ---------------------------------------------------------------------
//     std::string           mtlFilename = filePath.filename().replace_extension(".mtl").string();
//     std::filesystem::path mtlPath     = filePath.parent_path() / mtlFilename;

//     out << "mtllib " << mtlFilename << "\n";

//     // ---------------------------------------------------------------------
//     // Material access
//     // ---------------------------------------------------------------------
//     const MaterialHandler* materialHandler = scene->materialHandler();
//     if (!materialHandler)
//     {
//         report.error("SceneFormatOBJ::save: scene->materialHandler() returned null");
//         report.status = SceneIOStatus::InvalidScene;
//         return false;
//     }

//     const auto& materials = materialHandler->materials();

//     // ---------------------------------------------------------------------
//     // Iterate over scene meshes
//     // ---------------------------------------------------------------------
//     int vertBase = 1;
//     int normBase = 1;
//     int texBase  = 1;

//     int unnamedCounter = 1;

//     const auto& sceneMeshes = scene->sceneMeshes();

//     for (const SceneMesh* sm : sceneMeshes)
//     {
//         if (!sm)
//             continue;

//         const SysMesh* mesh = sm->sysMesh();
//         if (!mesh)
//             continue;

//         std::string name = std::string(sm->name());
//         if (name.empty() || name == "Unnamed")
//             name = "Unnamed_" + std::to_string(unnamedCounter++);

//         out << "# OriginalName: " << name << "\n";
//         out << "o " << sanitizeName(name) << "\n";

//         const int normalMap = mesh->map_find(/*MESH_MAP_NORMALS*/ 0);
//         const int texMap    = mesh->map_find(/*MESH_MAP_UV0*/ 1);

//         // -----------------------------------------------------------------
//         // Write vertex positions (v)
//         // -----------------------------------------------------------------
//         for (int vi : mesh->all_verts())
//         {
//             const glm::vec3 pos = mesh->vert_position(vi);
//             out << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
//         }

//         // -----------------------------------------------------------------
//         // Collect used face-varying map-verts (normals / texcoords)
//         // -----------------------------------------------------------------
//         std::vector<int32_t> usedNormIds;
//         usedNormIds.reserve(1024);
//         std::vector<int32_t> usedTexIds;
//         usedTexIds.reserve(1024);

//         std::unordered_set<int32_t> seenN;
//         std::unordered_set<int32_t> seenT;

//         if (normalMap != -1 || texMap != -1)
//         {
//             for (int pi : mesh->all_polys())
//             {
//                 if (normalMap != -1)
//                 {
//                     const auto& pn = mesh->map_poly_verts(normalMap, pi);
//                     for (int id : pn)
//                     {
//                         if (id >= 0 && !seenN.count(id))
//                         {
//                             seenN.insert(id);
//                             usedNormIds.push_back(id);
//                         }
//                     }
//                 }
//                 if (texMap != -1)
//                 {
//                     const auto& pt = mesh->map_poly_verts(texMap, pi);
//                     for (int id : pt)
//                     {
//                         if (id >= 0 && !seenT.count(id))
//                         {
//                             seenT.insert(id);
//                             usedTexIds.push_back(id);
//                         }
//                     }
//                 }
//             }
//         }

//         // -----------------------------------------------------------------
//         // Emit vt / vn and build remaps (mapVertId -> OBJ 1-based index)
//         // -----------------------------------------------------------------
//         std::unordered_map<int32_t, int32_t> texcoordRemap;
//         std::unordered_map<int32_t, int32_t> normalRemap;

//         if (texMap != -1)
//         {
//             int written = 0;
//             for (int id : usedTexIds)
//             {
//                 const glm::vec2 uv = glm::make_vec2(mesh->map_vert_position(texMap, id));
//                 texcoordRemap[id]  = texBase + written;
//                 out << "vt " << uv.x << " " << uv.y << "\n";
//                 ++written;
//             }
//             texBase += written;
//         }

//         if (normalMap != -1)
//         {
//             int written = 0;
//             for (int id : usedNormIds)
//             {
//                 const glm::vec3 n = glm::make_vec3(mesh->map_vert_position(normalMap, id));
//                 normalRemap[id]   = normBase + written;
//                 out << "vn " << n.x << " " << n.y << " " << n.z << "\n";
//                 ++written;
//             }
//             normBase += written;
//         }

//         // -----------------------------------------------------------------
//         // Group polygons by material and write faces
//         // -----------------------------------------------------------------
//         std::map<int, std::vector<int>> polysByMat;
//         for (int pi : mesh->all_polys())
//         {
//             const int matIndex = mesh->poly_material(pi);
//             polysByMat[matIndex].push_back(pi);
//         }

//         for (const auto& [matIndex, polyList] : polysByMat)
//         {
//             const std::string matName =
//                 (matIndex >= 0 && matIndex < static_cast<int>(materials.size()))
//                     ? materials[matIndex].name()
//                     : std::string("Default");

//             out << "usemtl " << sanitizeName(matName) << "\n";

//             for (int pi : polyList)
//             {
//                 const auto& verts = mesh->poly_verts(pi);
//                 const auto& pn    = (normalMap != -1) ? mesh->map_poly_verts(normalMap, pi) : SysPolyVerts{};
//                 const auto& pt    = (texMap != -1) ? mesh->map_poly_verts(texMap, pi) : SysPolyVerts{};

//                 const bool hasN  = (normalMap != -1) && (pn.size() == verts.size());
//                 const bool hasUV = (texMap != -1) && (pt.size() == verts.size());

//                 out << "f";
//                 for (size_t i = 0; i < verts.size(); ++i)
//                 {
//                     const int vIdx = vertBase + verts[i];

//                     if (hasUV && hasN)
//                     {
//                         out << " " << vIdx << "/" << texcoordRemap.at(pt[i]) << "/" << normalRemap.at(pn[i]);
//                     }
//                     else if (hasUV)
//                     {
//                         out << " " << vIdx << "/" << texcoordRemap.at(pt[i]);
//                     }
//                     else if (hasN)
//                     {
//                         out << " " << vIdx << "//" << normalRemap.at(pn[i]);
//                     }
//                     else
//                     {
//                         out << " " << vIdx;
//                     }
//                 }
//                 out << "\n";
//             }
//         }

//         vertBase += mesh->num_verts();
//     }

//     out.close();
//     if (!out)
//     {
//         report.error("SceneFormatOBJ::save: write error while closing OBJ file");
//         report.status = SceneIOStatus::WriteError;
//         return false;
//     }

//     // ---------------------------------------------------------------------
//     // Save MTL library
//     // ---------------------------------------------------------------------
//     if (!saveMaterialLibrary(scene, mtlPath))
//     {
//         if (report.status == SceneIOStatus::Ok)
//             report.status = SceneIOStatus::WriteError;

//         report.error("SceneFormatOBJ::save: failed to write MTL file: " + mtlPath.string());
//         return false;
//     }

//     return true;
// }

// bool ObjSceneFormat::saveMaterialLibrary(const Scene* scene, const std::filesystem::path& filePath)
// {
//     const auto&   materials = scene->materialHandler()->materials();
//     std::ofstream out(filePath);
//     if (!out)
//     {
//         std::cerr << "Could not open material file for writing: " << filePath << "\n";
//         return false;
//     }

//     out << std::fixed << std::setprecision(6);

//     for (const auto& mat : materials)
//     {
//         const MtlFields mtl = toMTL(mat, scene);

//         out << "newmtl " << sanitizeName(mat.name()) << "\n";
//         out << "Ka " << mtl.Ka.r << " " << mtl.Ka.g << " " << mtl.Ka.b << "\n";
//         out << "Kd " << mtl.Kd.r << " " << mtl.Kd.g << " " << mtl.Kd.b << "\n";
//         out << "Ks " << mtl.Ks.r << " " << mtl.Ks.g << " " << mtl.Ks.b << "\n";
//         out << "Ke " << mtl.Ke.r << " " << mtl.Ke.g << " " << mtl.Ke.b << "\n";
//         out << "Tf " << mtl.Tf.r << " " << mtl.Tf.g << " " << mtl.Tf.b << "\n";
//         out << "Tr " << mtl.Tr << "\n";
//         out << "Ns " << mtl.Ns << "\n";
//         out << "Ni " << mtl.Ni << "\n";
//         out << "d " << mtl.d << "\n";

//         const auto dir = filePath.parent_path();
//         if (!mtl.map_Kd.empty())
//             out << "map_Kd " << PathUtil::relativeSanitized(mtl.map_Kd, dir) << "\n";
//         if (!mtl.map_bump.empty())
//             out << "map_bump " << PathUtil::relativeSanitized(mtl.map_bump, dir) << "\n";
//         if (!mtl.map_Ke.empty())
//             out << "map_Ke " << PathUtil::relativeSanitized(mtl.map_Ke, dir) << "\n";

//         out << "\n";
//     }
//     return true;
// }
// #endif

#include "ObjSceneFormat.hpp"

#include <SysMesh.hpp>
#include <SysObjLoader.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CoreUtilities.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "SceneMtlUtils.hpp"

// ------------------------------------------------------------
// OBJ has no header counts. These are only fallback reserves.
// The loader below reads the whole file into memory and parses
// with char pointers instead of iostreams/istringstream/stoi.
// ------------------------------------------------------------
static constexpr int32_t kObjReserveVerts = 2048;

namespace
{
    inline bool is_space(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    inline const char* skip_spaces(const char* p, const char* e)
    {
        while (p < e && (*p == ' ' || *p == '\t'))
            ++p;
        return p;
    }

    inline const char* skip_to_line_end(const char* p, const char* e)
    {
        while (p < e && *p != '\n')
            ++p;
        return p;
    }

    inline const char* next_line(const char* p, const char* e)
    {
        p = skip_to_line_end(p, e);
        if (p < e && *p == '\n')
            ++p;
        return p;
    }

    inline std::string read_token_string(const char*& p, const char* e)
    {
        p             = skip_spaces(p, e);
        const char* b = p;
        while (p < e && !is_space(*p))
            ++p;
        return std::string(b, p);
    }

    inline std::string read_rest_of_line_trimmed(const char*& p, const char* e)
    {
        p             = skip_spaces(p, e);
        const char* b = p;
        const char* q = p;
        while (q < e && *q != '\n' && *q != '\r')
            ++q;
        const char* r = q;
        while (r > b && is_space(*(r - 1)))
            --r;
        p = q;
        return std::string(b, r);
    }

    inline bool parse_float3(const char*& p, const char* e, glm::vec3& v)
    {
        p         = skip_spaces(p, e);
        char* out = nullptr;
        v.x       = std::strtof(p, &out);
        if (out == p)
            return false;
        p   = skip_spaces(out, e);
        v.y = std::strtof(p, &out);
        if (out == p)
            return false;
        p   = skip_spaces(out, e);
        v.z = std::strtof(p, &out);
        if (out == p)
            return false;
        p = out;
        return true;
    }

    inline bool parse_float2(const char*& p, const char* e, glm::vec2& v)
    {
        p         = skip_spaces(p, e);
        char* out = nullptr;
        v.x       = std::strtof(p, &out);
        if (out == p)
            return false;
        p   = skip_spaces(out, e);
        v.y = std::strtof(p, &out);
        if (out == p)
            return false;
        p = out;
        return true;
    }

    inline int resolve_obj_index(int idx, int size)
    {
        if (idx > 0)
            return idx - 1;
        if (idx < 0)
            return size + idx;
        return -1;
    }

    struct ObjIdx
    {
        int v = -1;
        int t = -1;
        int n = -1;
    };

    // Parses one OBJ face vertex token:
    //   v
    //   v/t
    //   v//n
    //   v/t/n
    // No allocation, no substr, no stoi.
    inline ObjIdx parse_face_vertex(const char*& p,
                                    const char*  e,
                                    int          posCount,
                                    int          texCount,
                                    int          normCount)
    {
        ObjIdx idx{};

        char*      out  = nullptr;
        const long vRaw = std::strtol(p, &out, 10);
        if (out == p)
            return idx;

        idx.v = resolve_obj_index(static_cast<int>(vRaw), posCount);
        p     = out;

        if (p >= e || *p != '/')
            return idx;

        ++p; // first slash

        // v//n
        if (p < e && *p == '/')
        {
            ++p; // second slash
            const long nRaw = std::strtol(p, &out, 10);
            if (out != p)
            {
                idx.n = resolve_obj_index(static_cast<int>(nRaw), normCount);
                p     = out;
            }
            return idx;
        }

        // v/t or v/t/n
        const long tRaw = std::strtol(p, &out, 10);
        if (out != p)
        {
            idx.t = resolve_obj_index(static_cast<int>(tRaw), texCount);
            p     = out;
        }

        if (p < e && *p == '/')
        {
            ++p;
            const long nRaw = std::strtol(p, &out, 10);
            if (out != p)
            {
                idx.n = resolve_obj_index(static_cast<int>(nRaw), normCount);
                p     = out;
            }
        }

        return idx;
    }

    struct ObjPrepassCounts
    {
        size_t positions   = 0;
        size_t normals     = 0;
        size_t texcoords   = 0;
        size_t faces       = 0;
        size_t faceCorners = 0;
        size_t objects     = 0;
    };

    inline bool starts_with_keyword(const char* p, const char* e, const char* kw, size_t len)
    {
        return static_cast<size_t>(e - p) >= len &&
               std::memcmp(p, kw, len) == 0 &&
               (p + len == e || is_space(p[len]));
    }

    inline ObjPrepassCounts prepass_obj_counts(const std::string& buffer)
    {
        ObjPrepassCounts c{};

        const char* p = buffer.data();
        const char* e = buffer.data() + buffer.size();

        while (p < e)
        {
            p = skip_spaces(p, e);
            if (p >= e)
                break;

            if (*p == '#')
            {
                p = next_line(p, e);
                continue;
            }

            if (*p == 'v')
            {
                if (p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
                    ++c.positions;
                else if (p + 2 < e && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t'))
                    ++c.normals;
                else if (p + 2 < e && p[1] == 't' && (p[2] == ' ' || p[2] == '\t'))
                    ++c.texcoords;
            }
            else if (*p == 'f' && p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
            {
                ++c.faces;
                const char* q = p + 2;
                while (q < e && *q != '\n' && *q != '\r')
                {
                    q = skip_spaces(q, e);
                    if (q >= e || *q == '\n' || *q == '\r')
                        break;

                    ++c.faceCorners;
                    while (q < e && !is_space(*q))
                        ++q;
                }
            }
            else if (*p == 'o' && p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
            {
                ++c.objects;
            }

            p = next_line(p, e);
        }

        return c;
    }

    inline int32_t reserve_for_new_mesh(const ObjPrepassCounts& counts)
    {
        if (counts.objects <= 1 && counts.positions <= static_cast<size_t>(std::numeric_limits<int32_t>::max()))
            return static_cast<int32_t>(std::max<size_t>(counts.positions, kObjReserveVerts));

        if (counts.objects > 1)
        {
            const size_t avg = std::max<size_t>(kObjReserveVerts, counts.positions / counts.objects);
            return static_cast<int32_t>(std::min<size_t>(avg, static_cast<size_t>(std::numeric_limits<int32_t>::max())));
        }

        return kObjReserveVerts;
    }

    inline void ensure_vector_size(std::vector<int>& v, int index)
    {
        if (index >= static_cast<int>(v.size()))
            v.resize(static_cast<size_t>(index) + 1, -1);
    }
} // namespace

bool ObjSceneFormat::load(Scene*                       scene,
                          const std::filesystem::path& filePath,
                          const LoadOptions&           options,
                          SceneIOReport&               report)
{
    TICK(OBJ_LOAD);

    // ---------------------------------------------------------
    // Basic validation
    // ---------------------------------------------------------
    if (scene == nullptr)
    {
        report.error("SceneFormatOBJ::load: scene is null");
        report.status = SceneIOStatus::InvalidScene;
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
    // Read whole OBJ into memory.
    // This makes the hot path pointer-based and avoids getline,
    // istringstream, substr, stoi, and repeated stream buffering.
    // ---------------------------------------------------------
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        report.status = SceneIOStatus::FileNotFound;
        report.error("Failed to open OBJ file: " + filePath.string());
        return false;
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize < 0)
    {
        report.status = SceneIOStatus::ReadError;
        report.error("Failed to determine OBJ file size: " + filePath.string());
        return false;
    }

    std::string buffer;
    buffer.resize(static_cast<size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    if (fileSize > 0 && !file.read(buffer.data(), fileSize))
    {
        report.status = SceneIOStatus::ReadError;
        report.error("Failed to read OBJ file: " + filePath.string());
        return false;
    }
    file.close();

    MaterialHandler* materials = scene->materialHandler();
    if (!materials)
    {
        report.status = SceneIOStatus::InvalidScene;
        report.error("SceneFormatOBJ::load: scene->materials() returned null");
        return false;
    }

    // ---------------------------------------------------------
    // Prepass: exact reserves for attribute arrays and stable
    // dense remap arrays. This is cheap compared with full import
    // and removes several realloc / resize costs on huge OBJs.
    // ---------------------------------------------------------
    const ObjPrepassCounts counts = prepass_obj_counts(buffer);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    positions.reserve(counts.positions);
    normals.reserve(counts.normals);
    texcoords.reserve(counts.texcoords);

    std::string matlib;

    std::uint32_t matIndex = 0;
    int           normMap  = -1;
    int           texMap   = -1;

    SceneMesh* sceneMesh   = nullptr;
    SysMesh*   currentMesh = nullptr;

    // Dense OBJ position index -> SysMesh vertex index.
    // The stamp array lets us reuse the same dense arrays for each
    // OBJ object without clearing/filling 1M+ entries per object.
    std::vector<int>      globalToLocal;
    std::vector<uint32_t> globalToLocalStamp;

    globalToLocal.assign(counts.positions, -1);
    globalToLocalStamp.assign(counts.positions, 0u);

    uint32_t meshStamp = 0u;

    auto nextMeshStamp = [&]() {
        ++meshStamp;
        if (meshStamp == 0u)
        {
            std::fill(globalToLocalStamp.begin(), globalToLocalStamp.end(), 0u);
            meshStamp = 1u;
        }
    };

    const int32_t meshReserve = reserve_for_new_mesh(counts);

    auto ensureDefaultMesh = [&]() {
        if (currentMesh)
            return;

        sceneMesh   = scene->createSceneMesh("Default");
        currentMesh = sceneMesh->sysMesh();
        currentMesh->reserve(meshReserve);
        normMap = currentMesh->map_create(/*MESH_MAP_NORMALS*/ 0, 0, 3);
        texMap  = currentMesh->map_create(/*MESH_MAP_UV0*/ 1, 0, 2);
        nextMeshStamp();
    };

    auto startNewMesh = [&](const std::string& name) {
        sceneMesh   = scene->createSceneMesh(name.empty() ? std::string("Object") : name);
        currentMesh = sceneMesh->sysMesh();
        currentMesh->reserve(meshReserve);
        normMap = currentMesh->map_create(/*MESH_MAP_NORMALS*/ 0, 0, 3);
        texMap  = currentMesh->map_create(/*MESH_MAP_UV0*/ 1, 0, 2);
        nextMeshStamp();
    };

    const char* p = buffer.data();
    const char* e = buffer.data() + buffer.size();

    while (p < e)
    {
        p = skip_spaces(p, e);
        if (p >= e)
            break;

        if (*p == '#')
        {
            p = next_line(p, e);
            continue;
        }

        // -----------------------------------------------------
        // Vertex attributes
        // -----------------------------------------------------
        if (*p == 'v')
        {
            if (p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
            {
                p += 2;
                glm::vec3 v{};
                if (parse_float3(p, e, v))
                    positions.push_back(v);
                p = next_line(p, e);
                continue;
            }

            if (p + 2 < e && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t'))
            {
                p += 3;
                glm::vec3 n{};
                if (parse_float3(p, e, n))
                    normals.push_back(n);
                p = next_line(p, e);
                continue;
            }

            if (p + 2 < e && p[1] == 't' && (p[2] == ' ' || p[2] == '\t'))
            {
                p += 3;
                glm::vec2 uv{};
                if (parse_float2(p, e, uv))
                    texcoords.push_back(uv);
                p = next_line(p, e);
                continue;
            }
        }

        // -----------------------------------------------------
        // Face
        // -----------------------------------------------------
        if (*p == 'f' && p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
        {
            ensureDefaultMesh();
            p += 2;

            SysPolyVerts pv;
            SysPolyVerts pn;
            SysPolyVerts pt;

            bool invalidFace = false;

            while (p < e && *p != '\n' && *p != '\r')
            {
                p = skip_spaces(p, e);
                if (p >= e || *p == '\n' || *p == '\r')
                    break;

                const ObjIdx idx = parse_face_vertex(
                    p,
                    e,
                    static_cast<int>(positions.size()),
                    static_cast<int>(texcoords.size()),
                    static_cast<int>(normals.size()));

                if (idx.v < 0 || idx.v >= static_cast<int>(positions.size()))
                {
                    invalidFace = true;
                    break;
                }

                // OBJ indices are dense. Use the pre-sized vector + stamp.
                // No hash lookup, no resize, no clearing between objects.
                int meshIdx = -1;
                if (static_cast<size_t>(idx.v) < globalToLocal.size() &&
                    globalToLocalStamp[static_cast<size_t>(idx.v)] == meshStamp)
                {
                    meshIdx = globalToLocal[static_cast<size_t>(idx.v)];
                }
                else
                {
                    meshIdx = currentMesh->create_vert(positions[static_cast<size_t>(idx.v)]);
                    if (static_cast<size_t>(idx.v) < globalToLocal.size())
                    {
                        globalToLocal[static_cast<size_t>(idx.v)]      = meshIdx;
                        globalToLocalStamp[static_cast<size_t>(idx.v)] = meshStamp;
                    }
                }

                pv.push_back(meshIdx);

                // Keep original editable semantics: normal/UV map verts are
                // unique per face corner. Do NOT cache/reuse vn/vt indices.
                if (normMap != -1 && idx.n >= 0 && idx.n < static_cast<int>(normals.size()))
                {
                    pn.push_back(currentMesh->map_create_vert(normMap, glm::value_ptr(normals[static_cast<size_t>(idx.n)])));
                }

                if (texMap != -1 && idx.t >= 0 && idx.t < static_cast<int>(texcoords.size()))
                {
                    pt.push_back(currentMesh->map_create_vert(texMap, glm::value_ptr(texcoords[static_cast<size_t>(idx.t)])));
                }
            }

            if (!invalidFace && pv.size() >= 3)
            {
                const int poly = currentMesh->create_poly(pv, matIndex);
                if (poly >= 0)
                {
                    if (pn.size() == pv.size())
                        currentMesh->map_create_poly(normMap, poly, pn);
                    if (pt.size() == pv.size())
                        currentMesh->map_create_poly(texMap, poly, pt);
                }
            }
            else if (invalidFace)
            {
                report.error("Invalid vertex index in face, skipping polygon.");
            }

            p = next_line(p, e);
            continue;
        }

        // -----------------------------------------------------
        // Object name
        // -----------------------------------------------------
        if (*p == 'o' && p + 1 < e && (p[1] == ' ' || p[1] == '\t'))
        {
            p += 2;
            std::string objName = read_token_string(p, e);
            startNewMesh(objName);
            p = next_line(p, e);
            continue;
        }

        // -----------------------------------------------------
        // Material library
        // -----------------------------------------------------
        if (starts_with_keyword(p, e, "mtllib", 6))
        {
            p += 6;
            matlib = read_rest_of_line_trimmed(p, e);
            p      = next_line(p, e);
            continue;
        }

        // -----------------------------------------------------
        // Material assignment
        // -----------------------------------------------------
        if (starts_with_keyword(p, e, "usemtl", 6))
        {
            p += 6;
            std::string matName = read_token_string(p, e);
            matIndex            = materials->createMaterial(matName);
            p                   = next_line(p, e);
            continue;
        }

        // Ignore unsupported OBJ commands: g, s, l, p, vp, etc.
        p = next_line(p, e);
    }

    // ---------------------------------------------------------
    // Load .mtl if present
    // ---------------------------------------------------------
    if (!matlib.empty())
    {
        const std::filesystem::path mtlPath = filePath.parent_path() / matlib;
        loadMaterialLibrary(scene, mtlPath);
    }

    TOCK(OBJ_LOAD);

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
        return false;

    MaterialHandler* materialHandler = scene->materialHandler();
    if (materialHandler == nullptr)
        return false;

    int         currentIndex = -1;
    std::string line;
    MtlFields   cur{};

    auto commit = [&]() {
        if (currentIndex < 0)
            return;
        Material& dst = materialHandler->material(currentIndex);
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
            commit();

            std::string name;
            iss >> name;

            currentIndex = materialHandler->createMaterial(name);
            cur          = MtlFields{};
            cur.name     = name;
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
        report.error("SceneFormatOBJ::save: failed to open OBJ file for writing: " + filePath.string());
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

    const auto& materials = materialHandler->materials();

    // ---------------------------------------------------------------------
    // Iterate over scene meshes
    // ---------------------------------------------------------------------
    int vertBase = 1;
    int normBase = 1;
    int texBase  = 1;

    int unnamedCounter = 1;

    const auto& sceneMeshes = scene->sceneMeshes();

    for (const SceneMesh* sm : sceneMeshes)
    {
        if (!sm)
            continue;

        const SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        std::string name = std::string(sm->name());
        if (name.empty() || name == "Unnamed")
            name = "Unnamed_" + std::to_string(unnamedCounter++);

        out << "# OriginalName: " << name << "\n";
        out << "o " << sanitizeName(name) << "\n";

        const int normalMap = mesh->map_find(/*MESH_MAP_NORMALS*/ 0);
        const int texMap    = mesh->map_find(/*MESH_MAP_UV0*/ 1);

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
        // Emit vt / vn and build remaps (mapVertId -> OBJ 1-based index)
        // -----------------------------------------------------------------
        std::unordered_map<int32_t, int32_t> texcoordRemap;
        std::unordered_map<int32_t, int32_t> normalRemap;

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
                    const int vIdx = vertBase + verts[i];

                    if (hasUV && hasN)
                    {
                        out << " " << vIdx << "/" << texcoordRemap.at(pt[i]) << "/" << normalRemap.at(pn[i]);
                    }
                    else if (hasUV)
                    {
                        out << " " << vIdx << "/" << texcoordRemap.at(pt[i]);
                    }
                    else if (hasN)
                    {
                        out << " " << vIdx << "//" << normalRemap.at(pn[i]);
                    }
                    else
                    {
                        out << " " << vIdx;
                    }
                }
                out << "\n";
            }
        }

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
    // Save MTL library
    // ---------------------------------------------------------------------
    if (!saveMaterialLibrary(scene, mtlPath))
    {
        if (report.status == SceneIOStatus::Ok)
            report.status = SceneIOStatus::WriteError;

        report.error("SceneFormatOBJ::save: failed to write MTL file: " + mtlPath.string());
        return false;
    }

    return true;
}

bool ObjSceneFormat::saveMaterialLibrary(const Scene* scene, const std::filesystem::path& filePath)
{
    const auto&   materials = scene->materialHandler()->materials();
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

        const auto dir = filePath.parent_path();
        if (!mtl.map_Kd.empty())
            out << "map_Kd " << PathUtil::relativeSanitized(mtl.map_Kd, dir) << "\n";
        if (!mtl.map_bump.empty())
            out << "map_bump " << PathUtil::relativeSanitized(mtl.map_bump, dir) << "\n";
        if (!mtl.map_Ke.empty())
            out << "map_Ke " << PathUtil::relativeSanitized(mtl.map_Ke, dir) << "\n";

        out << "\n";
    }
    return true;
}
