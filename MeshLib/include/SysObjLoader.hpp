#ifndef SYS_OBJ_LOADER_HPP_INCLUDED
#define SYS_OBJ_LOADER_HPP_INCLUDED

#include <glm/vec3.hpp>
#include <string>
#include <vector>

/// Represents the material properties used in OBJ files.
struct ObjMaterial
{
    std::string name;       ///< Material name (newmtl)
    glm::vec3 Ka;           ///< Ambient color
    glm::vec3 Kd;           ///< Diffuse color
    glm::vec3 Ks;           ///< Specular color
    glm::vec3 Ke;           ///< Emission color
    glm::vec3 Tf;           ///< Transmission filter (RGB)
    float Tr;               ///< Transparency (LW)
    float Ns;               ///< Specular exponent
    float Ni;               ///< Optical density (refraction index)
    float d;                ///< Dissolve (opacity)

           // Texture maps
    std::string map_Ka;     ///< Ambient texture map
    std::string map_Kd;     ///< Diffuse texture map
    std::string map_Ks;     ///< Specular texture map
    std::string map_Ke;     ///< Emission texture map
    std::string map_Tr;     ///< Opacity texture map
    std::string map_bump;   ///< Bump map
    std::string map_Ni;     ///< Refraction map
};

using ObjMaterials = std::vector<ObjMaterial>;

/// Loads an OBJ file along with its associated material file (MTL).
/// @param filepath - The full path of the OBJ file.
/// @param mesh - A valid SysMesh instance to be populated.
/// @param materials - A vector where materials are loaded from the MTL file.
/// @return True if loading succeeded, false otherwise.
bool loadObjToMesh(const std::string& filepath, class SysMesh* mesh, std::vector<ObjMaterial>& materials);

/// Saves a SysMesh to an OBJ file along with its associated material file (MTL).
/// @param filepath - The path to save the OBJ file.
/// @param mesh - The mesh to be saved.
/// @param materials - The list of materials to be written to the MTL file.
/// @return True if saving succeeded, false otherwise.
bool saveMeshToObj(const std::string& filepath, const class SysMesh* mesh, const std::vector<ObjMaterial>& materials);

ObjMaterial new_material(const std::string& name);

#endif // SYS_OBJ_LOADER_HPP_INCLUDED
