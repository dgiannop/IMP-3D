// GGX / Cook–Torrance

#version 450

layout(location = 0) in vec3 pos;              // view-space position
layout(location = 1) in vec3 nrm;              // view-space normal
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

// View-space directional light (matches view-space nrm and pos)
const vec3 kLightDirView = normalize(vec3(0.3, 0.7, 0.2));

// ------------------------------------------------------------------
// Helpers (GGX / Cook-Torrance)
// ------------------------------------------------------------------

float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    // Schlick approximation
    return F0 + (1.0 - F0) * pow(1.0 - saturate(cosTheta), 5.0);
}

// GGX / Trowbridge-Reitz normal distribution
float D_GGX(float NdotH, float alpha)
{
    // alpha = roughness^2
    float a2   = alpha * alpha;
    float d    = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-7);
}

// Schlick-GGX geometry term (for one direction)
float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-7);
}

// Smith geometry using Schlick-GGX for both view and light
float G_Smith(float NdotV, float NdotL, float k)
{
    float ggxV = G_SchlickGGX(NdotV, k);
    float ggxL = G_SchlickGGX(NdotL, k);
    return ggxV * ggxL;
}

void main()
{
    // -----------------------------
    // Fetch material (safe clamp)
    // -----------------------------
    int id = clamp(vMaterialId, 0, int(materials.length()) - 1);
    GpuMaterial mat = materials[id];

    // -----------------------------
    // Base color (albedo)
    // -----------------------------
    vec3 albedo = mat.baseColor;

    if (mat.baseColorTexture >= 0)
    {
        albedo *= texture(uTextures[mat.baseColorTexture], vUv).rgb;
    }

    // -----------------------------
    // Vectors (view space)
    // -----------------------------
    vec3 N = normalize(nrm);
    vec3 V = normalize(-pos);                 // camera at origin in view space
    vec3 L = normalize(kLightDirView);
    vec3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // If light is behind, only ambient/emissive
    // (still allow emissive later)
    if (NdotL <= 0.0 || NdotV <= 0.0)
    {
        vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
        vec3 ambient  = albedo * 0.20;
        fragColor = vec4(ambient + emissive, 1.0);
        return;
    }

    // -----------------------------
    // Material params
    // -----------------------------
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float metallic  = clamp(mat.metallic,  0.0,  1.0);

    // Microfacet convention: alpha = roughness^2
    float alpha = roughness * roughness;

    // Dielectric F0 from IOR (optional). Default to ~0.04 if ior invalid.
    float ior = max(mat.ior, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08)); // keep sane

    // Metals use albedo as F0, dielectrics use F0 from IOR (~0.04)
    vec3 F0 = mix(F0d, albedo, metallic);

    // -----------------------------
    // Cook-Torrance BRDF (GGX)
    // -----------------------------
    vec3  F = fresnelSchlick(VdotH, F0);
    float D = D_GGX(NdotH, alpha);

    // UE4-style k for direct lighting:
    // k = (roughness + 1)^2 / 8
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float G = G_Smith(NdotV, NdotL, k);

    // Specular BRDF
    // spec = (D*G*F) / (4*NdotV*NdotL)
    vec3 numerator   = D * G * F;
    float denom      = max(4.0 * NdotV * NdotL, 1e-7);
    vec3 specularBRDF = numerator / denom;

    // Diffuse term: Lambert with energy conservation
    // kd = (1 - F) * (1 - metallic)
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuseBRDF = kd * albedo / 3.14159265;

    // -----------------------------
    // Lighting (single directional)
    // -----------------------------
    vec3 radiance = vec3(1.0); // light color/intensity; keep 1 for now

    vec3 direct = (diffuseBRDF + specularBRDF) * radiance * NdotL;

    // Ambient (simple, non-IBL) — keep similar overall brightness
    vec3 ambient = albedo * 0.20;

    // Emissive
    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;

    vec3 color = ambient + direct + emissive;

    fragColor = vec4(color, 1.0);
}
