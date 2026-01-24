// ============================================================
// ShadedDraw.frag
// GGX / Cook–Torrance + cheap IBL-ish fill (no cubemap)
// + Normal mapping via derivative-based TBN (no tangents required)
//
// Notes:
// - Assumes baseColor/emissive textures are sampled as sRGB->linear via Vulkan SRGB formats.
// - Assumes normal + MRAO textures are sampled as linear UNORM formats.
// - If normals look “inside out”, flip normalTS.y (comment marked below).
// ============================================================

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

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - saturate(cosTheta), 5.0);
}

float D_GGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-7);
}

float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-7);
}

float G_Smith(float NdotV, float NdotL, float k)
{
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

// Cheap “studio” environment (view-space dir)
vec3 studioEnv(vec3 dir)
{
    dir = normalize(dir);

    // Base gradient (ceiling bright, floor dark)
    float t = saturate(dir.y * 0.5 + 0.5);
    vec3 top    = vec3(1.0, 1.0, 1.05) * 1.6;
    vec3 bottom = vec3(0.03, 0.03, 0.035) * 0.7;
    vec3 col = mix(bottom, top, pow(t, 0.55));

    // Two big softboxes to create broad reflections
    vec3 box0 = normalize(vec3(0.15, 0.85, 0.35));
    float b0 = saturate(dot(dir, box0));
    col += vec3(1.0, 0.98, 0.95) * pow(b0, 25.0) * 4.5;

    vec3 box1 = normalize(vec3(-0.55, 0.65, 0.50));
    float b1 = saturate(dot(dir, box1));
    col += vec3(0.95, 0.98, 1.0) * pow(b1, 35.0) * 3.5;

    // Gentle horizon wrap
    col += vec3(0.20, 0.25, 0.30) * (1.0 - abs(dir.y)) * 0.25;

    return col;
}

// ------------------------------------------------------------
// Normal mapping (no tangents): build TBN from derivatives
// ------------------------------------------------------------
vec3 sampleNormalMapTS(int normalTex, vec2 uv)
{
    // Normal maps are LINEAR textures (NOT sRGB)
    vec3 n = texture(uTextures[normalTex], uv).xyz * 2.0 - 1.0;

    // If the normal map appears inverted, uncomment:
    // n.y = -n.y;

    return normalize(n);
}

mat3 tbnFromDerivatives(vec3 P, vec3 N, vec2 uv)
{
    vec3 dpdx = dFdx(P);
    vec3 dpdy = dFdy(P);

    vec2 dtdx = dFdx(uv);
    vec2 dtdy = dFdy(uv);

    // Tangent/bitangent from position/uv derivatives
    vec3 T = dpdx * dtdy.y - dpdy * dtdx.y;
    vec3 B = dpdy * dtdx.x - dpdx * dtdy.x;

    float invLenT = inversesqrt(max(dot(T, T), 1e-20));
    float invLenB = inversesqrt(max(dot(B, B), 1e-20));
    T *= invLenT;
    B *= invLenB;

    // Re-orthonormalize
    T = normalize(T - N * dot(N, T));
    B = normalize(cross(N, T));

    return mat3(T, B, N);
}

void main()
{
    // -----------------------------
    // Material fetch (safe clamp)
    // -----------------------------
    int matCount = int(materials.length());
    int id = (matCount > 0) ? clamp(vMaterialId, 0, matCount - 1) : 0;

    GpuMaterial mat =
        (matCount > 0)
            ? materials[id]
            : GpuMaterial(
                  vec3(1,0,1), 1.0,
                  vec3(0), 0.0,
                  0.5, 0.0, 1.5, 0.0,
                  -1, -1, -1, -1);

    // -----------------------------
    // Base color (albedo) [assume SRGB sampling via Vulkan]
    // -----------------------------
    vec3 albedo = mat.baseColor;
    if (mat.baseColorTexture >= 0)
        albedo *= texture(uTextures[mat.baseColorTexture], vUv).rgb;

    // -----------------------------
    // Vectors (view space)
    // -----------------------------
    vec3 N = normalize(nrm);
    vec3 V = normalize(-pos); // camera at origin in view space

    // Apply normal map (if present) using derivative-based TBN
    if (mat.normalTexture >= 0)
    {
        mat3 TBN = tbnFromDerivatives(pos, N, vUv);
        vec3 nTS = sampleNormalMapTS(mat.normalTexture, vUv);
        N = normalize(TBN * nTS);
    }

    // -----------------------------
    // Material scalars (+ MRAO pack)
    // -----------------------------
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float metallic  = clamp(mat.metallic,  0.0,  1.0);
    float ao        = 1.0;

    if (mat.mraoTexture >= 0)
    {
        // glTF: R=AO, G=roughness, B=metallic (all linear)
        vec3 mrao = texture(uTextures[mat.mraoTexture], vUv).rgb;
        ao        = clamp(mrao.r, 0.0, 1.0);

        // Drive params (common for glTF assets)
        roughness = clamp(roughness * mrao.g, 0.04, 1.0);
        metallic  = clamp(metallic  * mrao.b, 0.0,  1.0);
    }

    float alpha = roughness * roughness;

    // Dielectric F0 from IOR (optional; keep your logic)
    float ior = max(mat.ior, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08));

    vec3 F0 = mix(F0d, albedo, metallic);

    // -----------------------------
    // Direct lighting (single directional) — GGX
    // -----------------------------
    vec3 L = normalize(kLightDirView);
    vec3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    vec3 direct = vec3(0.0);
    vec3 F_atV  = fresnelSchlick(NdotV, F0);

    if (NdotL > 0.0 && NdotV > 0.0)
    {
        vec3  F = fresnelSchlick(VdotH, F0);
        float D = D_GGX(NdotH, alpha);

        float r = roughness + 1.0;
        float k = (r * r) / 8.0;
        float G = G_Smith(NdotV, NdotL, k);

        vec3  numerator     = D * G * F;
        float denom         = max(4.0 * NdotV * NdotL, 1e-7);
        vec3  specularBRDF  = numerator / denom;

        vec3 kd          = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuseBRDF = kd * albedo / 3.14159265;

        vec3 radiance = vec3(1.0);
        direct = (diffuseBRDF + specularBRDF) * radiance * NdotL;
    }

    // -----------------------------
    // Cheap IBL-ish indirect (no cubemap)
    // -----------------------------
    // Diffuse hemispherical fill based on normal
    vec3 hemi = studioEnv(N);
    vec3 kdI  = (vec3(1.0) - F_atV) * (1.0 - metallic);
    vec3 diffIBL = kdI * albedo * hemi * 0.25;

    // Specular env from reflection direction (broad reflections)
    vec3 R = reflect(-V, N);
    vec3 envSpec = studioEnv(R);

    float smoothness = 1.0 - roughness;
    vec3 specIBL = envSpec * F_atV * (0.15 + 0.35 * smoothness);

    // AO affects indirect
    diffIBL *= ao;
    specIBL *= mix(1.0, ao, 0.5);

    // -----------------------------
    // Emissive (if any) [assume SRGB sampling via Vulkan]
    // -----------------------------
    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
    if (mat.emissiveTexture >= 0)
        emissive *= texture(uTextures[mat.emissiveTexture], vUv).rgb;

    vec3 color = direct + diffIBL + specIBL + emissive;

    // Output opacity for future alpha paths (still opaque pipeline by default)
    fragColor = vec4(color, mat.opacity);
}
