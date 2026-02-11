//==============================================================
// Line.vert (simple thin line with depth bias)
//==============================================================
#version 450

layout(location = 0) in vec3 inPos;

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

// Must match Renderer::PushConstants layout for model/color
layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
    // If you ever want it, you can also add:
    // layout(offset = 80) vec4 overlayParams;
} pc;

void main()
{
    // World-space → view → clip using unified CameraUBO
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position   = ubo.proj * ubo.view * worldPos;

    // Push toward camera a tiny amount in clip space
    // This is stable across projection and avoids z-fighting.
    gl_Position.z -= 1e-4 * gl_Position.w;
}
