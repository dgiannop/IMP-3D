//==============================================================
// SolidDraw.vert
//==============================================================
#version 450

layout(location = 0) in vec3 vert;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 uvCo;
layout(location = 3) in int  inMaterialId;

layout(set = 0, binding = 0) uniform MvpUBO {
    mat4 proj;
    mat4 view;
} ubo;

layout(push_constant) uniform PC {
    mat4 model;
    vec4 color;
} pc;

layout(location = 0) out vec3 pos;
layout(location = 1) out vec3 nrm;
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out int vMaterialId;

void main()
{
    // World position
    vec4 worldPos = pc.model * vec4(vert, 1.0);

    // View-space position
    vec4 viewPos = ubo.view * worldPos;
    pos = viewPos.xyz;

    // View-space normal (uses view * model)
    mat3 normMtx = transpose(inverse(mat3(ubo.view * pc.model)));
    nrm = normalize(normMtx * norm);

    vUv          = uvCo;
    vMaterialId  = inMaterialId;   // Pass through

    gl_Position = ubo.proj * viewPos;
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
//   mat3 normMtx = transpose(inverse(mat3(ubo.view * ubo.model)));
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
//   mat3 normMtx = transpose(inverse(mat3(ubo.model)));
//   nrm = normalize(normMtx * norm);
//
//   Use this when I add Light objects to Scene and want
//   light to remain steady while camera orbits.
//
// ---------------------------------------------
