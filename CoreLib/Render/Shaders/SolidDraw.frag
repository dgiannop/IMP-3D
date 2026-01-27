//==============================================================
// SolidDraw.frag
//==============================================================
#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in int vMaterialId;

struct GpuMaterial
{
    vec3  baseColor;
    float opacity;

    vec3  emissiveColor;
    float emissiveIntensity;

    float roughness;
    float metallic;
    float ior;
    float pad0;

    int baseColorTexture;
    int normalTexture;
    int mraoTexture;
    int emissiveTexture;
};

layout(std430, set = 1, binding = 0) readonly buffer MaterialBuffer
{
    GpuMaterial materials[];
};

const int kMaxTextureCount = 512;
layout(set = 1, binding = 1) uniform sampler2D uTextures[kMaxTextureCount];

// ------------------------------------------------------------
// Lights UBO (std140) - set=0 binding=1
// Matches your C++ GpuLightsUBO layout (vec4-based).
// ------------------------------------------------------------
struct GpuLight
{
    vec4 pos_type;         // xyz = pos (VS) or unused, w = type
    vec4 dir_range;        // xyz = dir (VS), w = range/unused
    vec4 color_intensity;  // rgb = color, a = intensity
    vec4 spot_params;      // x = innerCos, y = outerCos, zw unused
};

layout(std140, set = 0, binding = 1) uniform LightsUBO
{
    uvec4 info;     // x = lightCount
    vec4  ambient;  // rgb = ambient, a = ambientStrength (optional)
    GpuLight lights[64];
} uLights;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 N = normalize(nrm);

    // --- material base ---
    int id = clamp(vMaterialId, 0, int(materials.length()) - 1);
    GpuMaterial mat = materials[id];

    vec3 base = mat.baseColor;

    if (mat.baseColorTexture >= 0)
        base *= texture(uTextures[mat.baseColorTexture], vUv).rgb;

    // --- lighting ---
    float ambientTerm = 0.25; // fallback
    if (uLights.ambient.a > 0.0)
        ambientTerm = uLights.ambient.a;

    vec3 lit = base * ambientTerm;

    uint lightCount = uLights.info.x;
    for (uint i = 0u; i < lightCount; ++i)
    {
        // type encoded in pos_type.w
        uint t = uint(uLights.lights[i].pos_type.w + 0.5);

        // Directional only for now
        if (t == 0u)
        {
            vec3 L = normalize(uLights.lights[i].dir_range.xyz); // VIEW SPACE
            float NdotL = max(dot(N, L), 0.0);
            NdotL = pow(NdotL, 0.75); // soften falloff

            vec3  c = uLights.lights[i].color_intensity.rgb;
            float I = uLights.lights[i].color_intensity.a;

            lit += base * (c * (I * NdotL));
        }
    }

    fragColor = vec4(lit, 1.0);
}
