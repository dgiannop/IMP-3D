/**************************
Grid.vert
***************************/
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

// Camera UBO: must match Renderer::CameraUBO (set = 0, binding = 0)
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;        // VIEW  -> CLIP
    mat4 view;        // WORLD -> VIEW
    mat4 viewProj;    // WORLD -> CLIP

    mat4 invProj;     // CLIP  -> VIEW
    mat4 invView;     // VIEW  -> WORLD
    mat4 invViewProj; // CLIP  -> WORLD

    vec4 camPos;      // world-space camera position
    vec4 viewport;    // (w, h, 1/w, 1/h)
    vec4 clearColor;  // RT clear color
} ubo;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
} pc;

void main()
{
    vColor = inColor; // per-vertex color

    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    vec4 viewPos  = ubo.view * worldPos;
    gl_Position   = ubo.proj * viewPos;
}
