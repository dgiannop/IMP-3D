//==============================================================
// OverlayFill.vert (filled overlay triangles)
//==============================================================
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

layout(set = 0, binding = 0) uniform MvpUBO {
    mat4 proj;
    mat4 view;
} ubo;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
    layout(offset = 80) vec4 overlayParams; // xy = viewport px (unused here)
} pc;

void main()
{
    mat4 mvp = ubo.proj * ubo.view * pc.model;
    gl_Position = mvp * vec4(inPos, 1.0);
    vColor = inColor;
}
