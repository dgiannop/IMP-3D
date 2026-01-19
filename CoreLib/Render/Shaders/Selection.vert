#version 450

// Position only, from unique vertex buffer (binding 0, location 0)
layout(location = 0) in vec3 inPos;

layout(set = 0, binding = 0) uniform MvpUBO
{
    mat4 proj;
    mat4 view;
} ubo;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
} pc;

void main()
{
    // World space
    vec4 worldPos = pc.model * vec4(inPos, 1.0);

    // Clip space
    gl_Position = ubo.proj * ubo.view * worldPos;

    gl_Position.z -= 2e-4 * gl_Position.w;   // slightly more than regular edges

    // Used only when pipeline topology is POINT_LIST
    gl_PointSize = 8.0;
}
