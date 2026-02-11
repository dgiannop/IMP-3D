//==============================================================
// ShadedDraw.vert
//  - Shared vertex interface for Solid / Shaded pipelines
//  - Uses unified CameraUBO (set=0, binding=0)
//==============================================================
#version 450

// ------------------------------
// Vertex attributes
// ------------------------------
layout(location = 0) in vec3 vert;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 uvCo;
layout(location = 3) in int  inMaterialId;

// ------------------------------
// Camera UBO (set = 0, binding = 0)
// Matches Renderer::CameraUBO
// ------------------------------
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    mat4 viewProj;

    mat4 invProj;
    mat4 invView;
    mat4 invViewProj;

    vec4 camPos;
    vec4 viewport;
    vec4 clearColor;
} uCamera;

// ------------------------------
// Push constants (model + debug color)
// ------------------------------
layout(push_constant) uniform PC
{
    mat4 model;
    vec4 color;
} pc;

// ------------------------------
// Varyings to fragment shader
// ------------------------------
layout(location = 0) out vec3 pos;             // view-space position
layout(location = 1) out vec3 nrm;             // view-space normal
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out int vMaterialId;

void main()
{
    // World position
    vec4 worldPos = pc.model * vec4(vert, 1.0);

    // View-space position
    vec4 viewPos = uCamera.view * worldPos;
    pos = viewPos.xyz;

    // View-space normal (headlight-style: view * model)
    mat3 normMtx = transpose(inverse(mat3(uCamera.view * pc.model)));
    nrm = normalize(normMtx * norm);

    vUv         = uvCo;
    vMaterialId = inMaterialId;

    gl_Position = uCamera.proj * viewPos;
}

// ---------------------------------------------
// Lighting mode reminder for IMP3D
// ---------------------------------------------
//
// ★ View-space lighting (current behavior)
//
//   mat3 normMtx = transpose(inverse(mat3(ubo.view * pc.model)));
//   nrm = normalize(normMtx * norm);
//
//   Both N and pos are in VIEW SPACE.
//   Lights in the fragment shader must also be expressed in view space.
//
// ---------------------------------------------
//
// ★ World-space lighting (future option)
//
//   For world-space shading, you would instead compute:
//
//     mat3 normMtx = transpose(inverse(mat3(pc.model)));
//     vec3 worldN  = normalize(normMtx * norm);
//     vec3 worldP  = (pc.model * vec4(vert, 1.0)).xyz;
//
//   and adjust the fragment shader to expect world-space inputs.
//
// ---------------------------------------------
