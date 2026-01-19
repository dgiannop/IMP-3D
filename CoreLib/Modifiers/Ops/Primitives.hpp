#pragma once

#include <glm/glm.hpp>

class SysMesh;

namespace Primitives
{
    /**
     * @brief Creates a segmented box with per-face normals and a fixed 3×4 UV atlas.
     * @param mesh Target mesh
     * @param center Box center
     * @param size Overall size of the box (outer dimensions)
     * @param segs Face segment counts per dimension
     */
    void createBox(SysMesh* mesh, glm::vec3 center, glm::vec3 size, glm::ivec3 segs);

    /**
     * @brief Creates a UV sphere with configurable axis orientation and optional smooth shading.
     * @param mesh   Target mesh
     * @param axis   Axis orientation (permutation/sign for X, Y, Z)
     * @param center Sphere center
     * @param radius Radius per axis (allows ellipsoid shapes)
     * @param rings  Number of latitude segments
     * @param sides  Number of longitude segments
     * @param smooth If true, generates smooth vertex normals; otherwise per-face normals
     */
    void createSphere(SysMesh* mesh, glm::vec3 center, glm::ivec3 axis, glm::vec3 radius, int rings, int sides, bool smooth);

    /**
     * @brief Creates a solid cylinder with face-varying normals and a fixed UV layout:
     *        side strip in bottom half, and two cap islands packed in the top half.
     *
     * UV layout:
     *   - Side:       U ∈ [0, 1],     V ∈ [0.0, 0.5]
     *   - Bottom cap: packed left:    U ∈ [0.0, 0.5], V ∈ [0.5, 1.0]
     *   - Top cap:    packed right:   U ∈ [0.5, 1.0], V ∈ [0.5, 1.0]
     *
     * Axis is the cylinder up direction (major axis via ivec3: e.g. {0,1,0}).
     *
     * @param mesh     Target mesh
     * @param center   Cylinder center
     * @param axis     Cylinder up axis (major axis)
     * @param radius   Cylinder radius
     * @param height   Cylinder height
     * @param sides    Radial sides (>= 3)
     * @param segs     Height segments (>= 1)
     * @param caps     Whether to create top/bottom caps
     */
    void createCylinder(SysMesh* mesh, glm::vec3 center, glm::ivec3 axis, float radius, float height, int sides, int segs, bool caps);

} // namespace Primitives
