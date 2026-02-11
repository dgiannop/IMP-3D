//==============================================================
// Wireframe.vert
//  - Uses unified CameraUBO (set=0, binding=0)
//  - Push constants match Renderer::PushConstants
//==============================================================
#version 450

layout(location = 0) in vec3 inPos;

// ------------------------------------------------------------
// Camera UBO (shared with Solid/Shaded/Selection/RT)
// set = 0, binding = 0
// ------------------------------------------------------------
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;        // VIEW  -> CLIP
    mat4 view;        // WORLD -> VIEW
    mat4 viewProj;    // WORLD -> CLIP

    mat4 invProj;     // CLIP  -> VIEW
    mat4 invView;     // VIEW  -> WORLD
    mat4 invViewProj; // CLIP  -> WORLD

    vec4 camPos;      // world-space camera position (xyz, 1)
    vec4 viewport;    // (w, h, 1/w, 1/h)
    vec4 clearColor;  // RT clear color (unused here)
} uCamera;

// ------------------------------------------------------------
// Push constants (matches Renderer::PushConstants)
// ------------------------------------------------------------
layout(push_constant) uniform PC
{
    mat4 model;
    vec4 color;
    vec4 overlayParams; // unused here
} pc;

void main()
{
    vec4 worldPos = pc.model * vec4(inPos, 1.0);

    // Clip space (you can also use uCamera.viewProj * worldPos)
    gl_Position = uCamera.proj * uCamera.view * worldPos;

    // Optional: push toward camera slightly to avoid z-fighting
    // gl_Position.z -= 1e-4 * gl_Position.w;
}
