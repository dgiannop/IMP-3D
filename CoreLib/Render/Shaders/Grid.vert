#version 450

layout(location = 0) in vec3 inPos;

layout(set = 0, binding = 0) uniform MvpUBO {
    mat4 proj;
    mat4 view;
    // mat4 model;
} ubo;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
} pc;

void main()
{
    vec4 wp = pc.model * vec4(inPos, 1.0);
    vec4 vp = ubo.view  * wp;
    gl_Position = ubo.proj * vp;
}
