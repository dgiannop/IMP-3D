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

layout(location = 0) out vec4 fragColor;

// Simple view-space directional light (camera-space)
const vec3 kLightDirView = normalize(vec3(0.3, 0.7, 0.2));

void main()
{
    vec3 N = normalize(nrm);

    float NdotL = max(dot(N, kLightDirView), 0.0);

    float ambient = 0.25;
    float diffuse = NdotL;

    int id = clamp(vMaterialId, 0, int(materials.length()) - 1);
    GpuMaterial mat = materials[id];

    // Start with baseColor from material
    vec3 base = mat.baseColor;

    // If there is a baseColor texture, modulate by it
    if (mat.baseColorTexture >= 0)
    {
        // mat.baseColorTexture comes from TextureHandler index
        base *= texture(uTextures[mat.baseColorTexture], vUv).rgb;
    }

    vec3 color = base * (ambient + diffuse);
    fragColor  = vec4(color, 1.0);
}
