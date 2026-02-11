//==============================================================
// OverlayFill.vert (filled overlay triangles)
//==============================================================
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

// Unified CameraUBO (matches Renderer::CameraUBO at set=0,binding=0)
layout(set = 0, binding = 0) uniform CameraUBO
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

// Must match Renderer::PushConstants layout
layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
    layout(offset = 80) vec4 overlayParams; // xy = viewport px (unused here)
} pc;

void main()
{
    // World -> Clip using unified CameraUBO
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPos, 1.0);

    // Per-vertex color from attribute (not from push constants)
    vColor = inColor;
}
