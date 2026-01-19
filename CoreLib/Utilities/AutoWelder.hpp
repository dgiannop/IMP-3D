#pragma once

#include <SysMesh.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vector_query.hpp>
#include <glm/vec3.hpp>

class AutoWelder
{
public:
    /// @return A new vert index or an existing one if very close.
    int32_t operator()(SysMesh* mesh, const glm::vec3& pos)
    {
        for (int32_t vert_index : mesh->all_verts())
        {
            // if (glm::length(mesh->vert_position(vert_index) - pos) < 0.01f)
            // if (glm::length2(mesh->vert_position(vert_index) - pos) < 0.001f)
            if (glm::distance(mesh->vert_position(vert_index), pos) < 0.001f)
                return vert_index;
        }
        return mesh->create_vert(pos);
    }
};
