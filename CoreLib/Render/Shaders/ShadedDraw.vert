//==============================================================
// ShadedDraw.vert  (WORLD-space interface to match SOLID)
//==============================================================
#version 450

layout(location = 0) in vec3 vert;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 uvCo;
layout(location = 3) in int  inMaterialId;

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

layout(push_constant) uniform PC
{
    mat4 model;
    vec4 color;
} pc;

// WORLD-space varyings (match SOLID world version)
layout(location = 0) out vec3 posW;
layout(location = 1) out vec3 nrmW;
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out int vMaterialId;

void main()
{
    vec4 worldPos = pc.model * vec4(vert, 1.0);
    posW = worldPos.xyz;

    mat3 nrmMtx = transpose(inverse(mat3(pc.model)));
    nrmW = normalize(nrmMtx * norm);

    vUv         = uvCo;
    vMaterialId = inMaterialId;

    gl_Position = uCamera.viewProj * worldPos;
}
