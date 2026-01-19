#version 450

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
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position   = ubo.proj * ubo.view * worldPos;

    // push toward camera a tiny amount in clip space
    // This is stable across projection and avoids z-fighting.
    // gl_Position.z -= 1e-4 * gl_Position.w;
}
