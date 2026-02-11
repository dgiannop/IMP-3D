//==============================================================
// SolidDraw.vert
//  - Shared vertex interface for Solid / Shaded pipelines
//  - View-space position + normal for headlight-style lighting
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
// Matches Renderer::CameraUBO in C++
//==============================================================
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;        // VIEW  -> CLIP
    mat4 view;        // WORLD -> VIEW
    mat4 viewProj;    // WORLD -> CLIP

    mat4 invProj;     // CLIP  -> VIEW
    mat4 invView;     // VIEW  -> WORLD
    mat4 invViewProj; // CLIP  -> WORLD

    vec4 camPos;      // world-space camera position
    vec4 viewport;    // (width, height, 1/width, 1/height)
    vec4 clearColor;  // RT clear color (unused here)
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

    // View-space normal (uses view * model)
    mat3 normMtx = transpose(inverse(mat3(uCamera.view * pc.model)));
    nrm = normalize(normMtx * norm);

    vUv         = uvCo;
    vMaterialId = inMaterialId;

    gl_Position = uCamera.proj * viewPos;
}

// ---------------------------------------------
// NOTE — Lighting mode reminder for IMP3D
// ---------------------------------------------
//
// ★ Headlight mode (default viewport lighting)
//
//   Use view-space normals so light stays fixed to camera.
//   The model rotates under camera, but shading does NOT change
//   when viewport rotates → "DCC default headlight".
//
//   mat3 normMtx = transpose(inverse(mat3(uCamera.view * pc.model)));
//   nrm = normalize(normMtx * norm);
//
//   Use this only when REAL scene lights are not present.
//
// ---------------------------------------------
//
// ★ Scene lighting (actual lights in the scene)
//
//   Stop using headlight. Compute normals in WORLD space.
//   Light directions/positions must also be in WORLD space.
//   View matrix ONLY affects camera, not shading.
//
//   mat3 normMtx = transpose(inverse(mat3(pc.model)));
//   nrm = normalize(normMtx * norm);
//
//   Use this when Light objects are active in the Scene and you
//   want lighting to remain steady while camera orbits.
//
// ---------------------------------------------
