#include "SysObjLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "SysMesh.hpp"

// -------------------------------------------------------------------------------
// Convert a string to lower case
static std::string to_lower(std::string str)
{
    std::ranges::transform(str, str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

// -------------------------------------------------------------------------------
// @return an initialized material
ObjMaterial new_material(const std::string& name)
{
    ObjMaterial def_mat = {};
    def_mat.name = name;
    def_mat.Ka = glm::vec3(0.2f, 0.2f, 0.2f);
    def_mat.Kd = glm::vec3(0.8f, 0.8f, 0.8f);
    def_mat.Ks = glm::vec3(0.0f, 0.0f, 0.0f);
    def_mat.Ke = glm::vec3(0.0f, 0.0f, 0.0f);
    def_mat.Ns = 0.0f;
    def_mat.Ni = 1.0f;
    def_mat.d = 1.0f;
    return def_mat;
}

// -------------------------------------------------------------------------------
// Add a material to the list if it doesn't exist or return the index of existing
uint32_t add_material(const std::string& name, ObjMaterials& materials)
{
    for (uint16_t i = 0; i < materials.size(); ++i)
    {
        if (to_lower(name) == to_lower(materials[i].name))
        {
            return i;
        }
    }
    materials.push_back(new_material(name));
    return static_cast<uint16_t>(materials.size() - 1);
}

// -------------------------------------------------------------------------------
bool loadObjMaterialsFromFile(const std::string& filename, std::vector<ObjMaterial>& materials);
void writeObjMaterialsToFile(const std::string& filename, const std::vector<ObjMaterial>& materials);
// -------------------------------------------------------------------------------

bool loadObjToMesh(const std::string& filepath, SysMesh* mesh, std::vector<ObjMaterial>& materials)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open OBJ file: " << filepath << std::endl;
        return false;
    }

    uint32_t mat_index = 0;  // Default material index
    const int32_t norm_map = mesh->map_create(0, 0, 3);  // Normal map
    const int32_t text_map = mesh->map_create(1, 0, 2);  // Texture map

    std::string mat_lib;
    std::string line;

    while (std::getline(file, line))
    {
        std::istringstream sstream(line);
        std::string prefix;
        sstream >> prefix;

        if (prefix == "mtllib")
        {
            // Read material library (trim spaces)
            sstream >> mat_lib;
        }
        else if (prefix == "usemtl")
        {
            // Read material name (trim spaces)
            std::string mat_name;
            sstream >> mat_name;
            mat_index = add_material(mat_name, materials);
        }
        else if (prefix == "v")
        {
            // Vertex position
            glm::vec3 pos;
            sstream >> pos.x >> pos.y >> pos.z;
            mesh->create_vert(pos);
        }
        else if (prefix == "vn")
        {
            // Vertex normal
            glm::vec3 norm;
            sstream >> norm.x >> norm.y >> norm.z;
            mesh->map_create_vert(norm_map, &norm[0]);
        }
        else if (prefix == "vt")
        {
            // Texture coordinates
            glm::vec2 uv;
            sstream >> uv.x >> uv.y;
            mesh->map_create_vert(text_map, &uv[0]);
        }
        else if (prefix == "f")
        {
            // Face (polygon) processing
            SysPolyVerts pv, pn, pt;
            std::string vertex;
            while (sstream >> vertex)
            {
                std::istringstream vertexStream(vertex);
                std::string indexStr;
                int index;

                       // Parse vertex data (position, normal, texture)
                if (std::getline(vertexStream, indexStr, '/'))
                {
                    index = std::stoi(indexStr);
                    pv.push_back(index - 1);  // Vertex position
                }
                if (std::getline(vertexStream, indexStr, '/'))
                {
                    if (!indexStr.empty())
                    {
                        index = std::stoi(indexStr);
                        pt.push_back(index - 1);  // Texture coordinate
                    }
                }
                if (std::getline(vertexStream, indexStr, '/'))
                {
                    if (!indexStr.empty())
                    {
                        index = std::stoi(indexStr);
                        pn.push_back(index - 1);  // Normal
                    }
                }
            }

                   // Create polygons and associate with materials
            const int32_t poly_index = mesh->create_poly(pv, mat_index);
            if (pn.size() == pv.size()) mesh->map_create_poly(norm_map, poly_index, pn);
            if (pt.size() == pv.size()) mesh->map_create_poly(text_map, poly_index, pt);
        }
    }

    file.close();

    // Add default material if none exists
    if (materials.empty()) add_material("Default", materials);

    // Load material library if defined
    if (!mat_lib.empty())
    {
        std::filesystem::path filePath = filepath;
        const std::string mtlPath = filePath.parent_path().string() + "/" + mat_lib;
        loadObjMaterialsFromFile(mtlPath, materials);
    }

    // std::cout << " Verts: " << mesh->num_verts()
    //           << " Polys: " << mesh->num_polys()
    //           << " Edges: " << mesh->all_edges().size() << "\n";


    return true;
}

bool saveMeshToObj(const std::string& filepath, const SysMesh* mesh, const std::vector<ObjMaterial>& materials)
{
    std::filesystem::path filePath = filepath;
    const std::string mtlDstPath = filePath.replace_extension(".mtl").string();
    const std::string mtlLib = filePath.filename().replace_extension(".mtl").string();

    std::ofstream out(filepath);
    if (!out.is_open())
    {
        std::cerr << "Cannot open file for writting." << std::endl;
        return false;
    }

    out << std::fixed;

    out << "mtllib " + mtlLib << std::endl;

    const auto norm_map = mesh->map_find(0); // Normal vertex map
    const auto text_map = mesh->map_find(1); // UV vertex map

    std::vector<glm::vec3> norms;
    std::vector<glm::vec2> texts;

    struct PolyData
    {
        SysPolyVerts pv;
        SysPolyVerts pn;
        SysPolyVerts pt;
    };

    std::map<int32_t, std::pair<std::string, std::vector<PolyData>>> poly_map;

    for (int32_t poly_index: mesh->all_polys())
    {
        const SysPolyVerts& pv = mesh->poly_verts(poly_index);
        const SysPolyVerts& pn = mesh->map_poly_verts(norm_map, poly_index);
        const SysPolyVerts& pt = mesh->map_poly_verts(text_map, poly_index);

        const int32_t mat_index = mesh->poly_material(poly_index);


               ////
        if (materials.empty())
        {
            std::cerr << "[Save] No materials assigned — inserting Default material.\n";
            const_cast<std::vector<ObjMaterial>&>(materials).push_back(new_material("Default"));
        }

               ///
        poly_map[mat_index].first = materials[mat_index].name;

        PolyData poly_data;

        for (int32_t vert_index: pv)
        {
            poly_data.pv.push_back(vert_index + 1);
        }

        for (int32_t norm_index: pn)
        {
            norms.push_back(glm::make_vec3(mesh->map_vert_position(norm_map, norm_index)));
            poly_data.pn.push_back(static_cast<int32_t>(norms.size())); // Obj format faces are 1based
        }

        for (int32_t text_index: pt)
        {
            texts.push_back(glm::make_vec2(mesh->map_vert_position(text_map, text_index)));
            poly_data.pt.push_back(static_cast<int32_t>(texts.size())); // Obj format faces are 1based
        }
        poly_map[mat_index].second.push_back(poly_data);
    }

           // Output vertex positions
    for (int32_t index: mesh->all_verts())
    {
        const glm::vec3& pos = mesh->vert_position(index);
        out << "v " << pos.x << " " << pos.y << " " << pos.z << std::endl;
    }

           // Output normals
    for (const glm::vec3& vn: norms)
    {
        out << "vn " << vn.x << " " << vn.y << " " << vn.z << std::endl;
    }

           // Output UV coordinates
    for (const glm::vec2& vt: texts)
    {
        out << "vt " << vt.x << " " << vt.y << std::endl;
    }

    // Output faces
    for (auto& pair: poly_map)
    {
        out << "usemtl " << pair.second.first << std::endl;

        for (PolyData& data: pair.second.second)
        {
            const SysPolyVerts& pv = data.pv;
            const SysPolyVerts& pn = data.pn;
            const SysPolyVerts& pt = data.pt;

            out << "f";

            if (!pt.empty() && !pn.empty())
            {
                for (int32_t i = 0; i < pv.size(); ++i)
                {
                    out << " " << pv[i] << "/" << pt[i] << "/" << pn[i];
                }
            }
            else if (!pt.empty())
            {
                for (int32_t i = 0; i < pv.size(); ++i)
                {
                    out << " " << pv[i] << "/" << pt[i];
                }
            }
            else if (!pn.empty())
            {
                for (int32_t i = 0; i < pv.size(); ++i)
                {
                    out << " " << pv[i] << "//" << pn[i];
                }
            }
            else
            {
                for (int32_t i = 0; i < pv.size(); ++i)
                {
                    out << " " << pv[i];
                }
            }
            out << std::endl;
        }
    }
    out.close();

           // Write materials to .mtl file
    writeObjMaterialsToFile(mtlDstPath, materials);

    return true;
}

bool loadObjMaterialsFromFile(const std::string& filename, std::vector<ObjMaterial>& materials)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << "\n";
        return false;
    }

    uint32_t mat_index = 0;

           // ObjMaterial currentMaterial;
    std::string line;

    while (std::getline(file, line))
    {
        // Ignore empty lines and comments
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

               // Remove leading spaces or tabs
        line = line.substr(line.find_first_not_of(" \t"));

               // Check for new material
        if (line.substr(0, 7) == "newmtl ")
        {
            std::string mat_name = line.substr(7);
            mat_name = mat_name.substr(mat_name.find_first_not_of(" \t"));
            mat_index = add_material(mat_name, materials);
        }
        // Parse Ka (ambient color)
        else if (line.substr(0, 3) == "Ka ")
        {
            std::istringstream stream(line.substr(3));
            stream >> materials[mat_index].Ka.r >> materials[mat_index].Ka.g >> materials[mat_index].Ka.b;
        }
        // Parse Kd (diffuse color)
        else if (line.substr(0, 3) == "Kd ")
        {
            std::istringstream stream(line.substr(3));
            stream >> materials[mat_index].Kd.r >> materials[mat_index].Kd.g >> materials[mat_index].Kd.b;
        }
        // Parse Ks (specular color)
        else if (line.substr(0, 3) == "Ks ")
        {
            std::istringstream stream(line.substr(3));
            stream >> materials[mat_index].Ks.r >> materials[mat_index].Ks.g >> materials[mat_index].Ks.b;
        }
        // Parse Ke (emission color)
        else if (line.substr(0, 3) == "Ke ")
        {
            std::istringstream stream(line.substr(3));
            stream >> materials[mat_index].Ke.r >> materials[mat_index].Ke.g >> materials[mat_index].Ke.b;
        }
        // Parse Tf (transmission filter)
        else if (line.substr(0, 3) == "Tf ")
        {
            std::istringstream stream(line.substr(3));
            stream >> materials[mat_index].Tf.r >> materials[mat_index].Tf.g >> materials[mat_index].Tf.b;
        }
        // Parse Tr (transparency)
        else if (line.substr(0, 3) == "Tr ")
        {
            materials[mat_index].Tr = std::stof(line.substr(3));
        }
        // Parse Ns (specular exponent)
        else if (line.substr(0, 3) == "Ns ")
        {
            materials[mat_index].Ns = std::stof(line.substr(3));
        }
        // Parse Ni (optical density)
        else if (line.substr(0, 3) == "Ni ")
        {
            materials[mat_index].Ni = std::stof(line.substr(3));
        }
        // Parse d (dissolve)
        else if (line.substr(0, 2) == "d ")
        {
            materials[mat_index].d = std::stof(line.substr(2));
        }
        // Parse map_Ka (ambient texture map)
        else if (line.substr(0, 7) == "map_Ka ")
        {
            materials[mat_index].map_Ka = line.substr(7);
        }
        // Parse map_Kd (diffuse texture map)
        else if (line.substr(0, 7) == "map_Kd ")
        {
            materials[mat_index].map_Kd = line.substr(7);
        }
        // Parse map_Ks (specular texture map)
        else if (line.substr(0, 7) == "map_Ks ")
        {
            materials[mat_index].map_Ks = line.substr(7);
        }
        // Parse map_Ke (emission texture map)
        else if (line.substr(0, 7) == "map_Ke ")
        {
            materials[mat_index].map_Ke = line.substr(7);
        }
        // Parse map_Tr (opacity texture map)
        else if (line.substr(0, 7) == "map_Tr ")
        {
            materials[mat_index].map_Tr = line.substr(7);
        }
        // Parse map_bump (bump map)
        else if (line.substr(0, 8) == "map_bump ")
        {
            materials[mat_index].map_bump = line.substr(8);
        }
        // Parse map_Ni (refraction map)
        else if (line.substr(0, 7) == "map_Ni ")
        {
            materials[mat_index].map_Ni = line.substr(7);
        }
    }

    file.close();
    return true;
}

void writeObjMaterialsToFile(const std::string& filename, const std::vector<ObjMaterial>& materials)
{
    std::ofstream outFile(filename);

    if (!outFile)
    {
        std::cerr << "Error: Could not open file " << filename << " for writing.\n";
        return;
    }

    for (const auto& mtl : materials)
    {
        outFile << "newmtl " << mtl.name << "\n";
        // Write ambient color (Ka)
        outFile << "Ka " << mtl.Ka.r << " " << mtl.Ka.g << " " << mtl.Ka.b << "\n";
        // Write diffuse color (Kd)
        outFile << "Kd " << mtl.Kd.r << " " << mtl.Kd.g << " " << mtl.Kd.b << "\n";

               // Write specular color (Ks)
        if (mtl.Ks != glm::vec3(0.0f))
        {
            outFile << "Ks " << mtl.Ks.r << " " << mtl.Ks.g << " " << mtl.Ks.b << "\n";
        }

               // Write emission color (Ke)
        if (mtl.Ke != glm::vec3(0.0f))
        {
            outFile << "Ke " << mtl.Ke.r << " " << mtl.Ke.g << " " << mtl.Ke.b << "\n";
        }

               // Write transmission filter (Tf)
        if (mtl.Tf != glm::vec3(0.0f))
        {
            outFile << "Tf " << mtl.Tf.r << " " << mtl.Tf.g << " " << mtl.Tf.b << "\n";
        }

               // Write transparency (Tr)
        if (mtl.Tr != 1.0f)
        {
            outFile << "Tr " << mtl.Tr << "\n";
        }

               // Write specular exponent (Ns)
        if (mtl.Ns != 0.0f)
        {
            outFile << "Ns " << mtl.Ns << "\n";
        }

               // Write optical density (Ni) 1.f for air or 1.5f for glass
        outFile << "Ni " << mtl.Ni << "\n";

               // Write dissolve factor (d)
        if (mtl.d != 1.0f) // 1.f is fully opaque
        {
            outFile << "d " << mtl.d << "\n";
        }

               // Write texture maps if defined
        if (!mtl.map_Ka.empty())
        {
            outFile << "map_Ka " << mtl.map_Ka << "\n";
        }
        if (!mtl.map_Kd.empty())
        {
            outFile << "map_Kd " << mtl.map_Kd << "\n";
        }
        if (!mtl.map_Ks.empty())
        {
            outFile << "map_Ks " << mtl.map_Ks << "\n";
        }
        if (!mtl.map_Ke.empty())
        {
            outFile << "map_Ke " << mtl.map_Ke << "\n";
        }
        if (!mtl.map_Tr.empty())
        {
            outFile << "map_Tr " << mtl.map_Tr << "\n";
        }
        if (!mtl.map_bump.empty())
        {
            outFile << "map_bump " << mtl.map_bump << "\n";
        }
        if (!mtl.map_Ni.empty())
        {
            outFile << "map_Ni " << mtl.map_Ni << "\n";
        }

        outFile << "\n";
    }

    outFile.close();
    std::cout << "Materials written to " << filename << "\n";
}
