//==============================================================
// ShadedDraw.vert
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

layout(location = 0) out vec3 pos;             // view-space position
layout(location = 1) out vec3 nrm;             // view-space normal
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out int vMaterialId;

void main()
{
    vec4 worldPos = pc.model * vec4(vert, 1.0);

    vec4 viewPos = ubo.view * worldPos;
    pos = viewPos.xyz;

    mat3 normMtx = transpose(inverse(mat3(ubo.view * pc.model)));
    nrm = normalize(normMtx * norm);

    vUv         = uvCo;
    vMaterialId = inMaterialId;

    gl_Position = ubo.proj * viewPos;
}

/*
//==============================================================
// ShadedDraw.vert
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

layout(location = 0) out vec3 pos;             // view-space position
layout(location = 1) out vec3 nrm;             // view-space normal
layout(location = 2) out vec2 vUv;
layout(location = 3) flat out int vMaterialId;

void main()
{
    // World position
    vec4 worldPos = pc.model * vec4(vert, 1.0);

    // View-space position
    vec4 viewPos = ubo.view * worldPos;
    pos = viewPos.xyz;

    // View-space normal (headlight mode)
    mat3 normMtx = transpose(inverse(mat3(ubo.view * pc.model)));
    nrm = normalize(normMtx * norm);

    vUv         = uvCo;
    vMaterialId = inMaterialId;

    gl_Position = ubo.proj * viewPos;
}
*/
