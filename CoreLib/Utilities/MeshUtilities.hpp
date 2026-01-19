#ifndef MESH_UTILITIES_HPP_INCLUDED
#define MESH_UTILITIES_HPP_INCLUDED

#include <glm/glm.hpp>
#include <vector>

// void triangulateMesh(class SysMesh* mesh);

void centerMesh(class SysMesh* mesh);

void scaleMesh(class SysMesh* mesh, float amount = 1.f);

void checkMeshNormals(class SysMesh* mesh);

// bool loadFromFile(SysMesh* m_mesh, const std::filesystem::path& filename, bool triangulate = false);

// ----------------------------------------------------------
// CPU-side render data extraction
// ----------------------------------------------------------

struct MeshData
{
    std::vector<glm::vec3> verts; // 3 per triangle
    std::vector<glm::vec3> norms;
    std::vector<glm::vec2> uvPos;
    std::vector<uint32_t>  matIds;
};

// Coarse mesh → triangles
MeshData extractMeshData(const SysMesh* mesh);

// Edges → line list
std::vector<glm::vec3> extractMeshEdges(const SysMesh* mesh);

// Mesh verts whole range with removed for 1:1 indexing
std::vector<glm::vec3> extractMeshPositionsOnly(const SysMesh* sys); // SysMesh indexing: rename to extractUniqueSlotPositions
std::vector<uint32_t>  extractMeshTriIndices(const SysMesh* sys);    // indexing into extractMeshPositionsOnly (SysMesh indexing)

std::vector<glm::vec3> extractTriPositionsOnly(const SysMesh* mesh);
std::vector<glm::vec3> extractPolyNormasOnly(const SysMesh* mesh);

// Edges to vert indices
std::vector<uint32_t> extractMeshEdgeIndices(const SysMesh* mesh);

std::vector<uint32_t> extractSelectedVertices(const SysMesh* sys);

std::vector<uint32_t> extractSelectedEdges(const SysMesh* sys);

std::vector<uint32_t> extractSelectedPolyTriangles(const SysMesh* sys);

#endif
