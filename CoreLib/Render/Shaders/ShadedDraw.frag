#version 450

layout(location = 0) in vec3 pos;              // view-space position
layout(location = 1) in vec3 nrm;              // view-space normal
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in int vMaterialId;

layout(location = 0) out vec4 fragColor;

// ============================================================
// Materials
// ============================================================

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

// ============================================================
// Lights (matches GpuLightsUBO, VIEW SPACE)
// ============================================================

struct GpuLight
{
    vec4 pos_type;        // w = type
    vec4 dir_range;       // xyz = L (surface -> light), view-space
    vec4 color_intensity; // rgb = color, a = intensity
    vec4 spot_params;
};

layout(set = 0, binding = 1) uniform LightsUBO
{
    uvec4   info;         // x = lightCount
    vec4    ambient;      // rgb, a
    GpuLight lights[64];
} uLights;

// ============================================================
// Helpers
// ============================================================

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

// ------------------------------------------------------------
// Cheap studio environment (unchanged)
// ------------------------------------------------------------
vec3 studioEnv(vec3 dir)
{
    dir = normalize(dir);

    float t = saturate(dir.y * 0.5 + 0.5);
    vec3 top    = vec3(1.0, 1.0, 1.05) * 1.6;
    vec3 bottom = vec3(0.03, 0.03, 0.035) * 0.7;
    vec3 col = mix(bottom, top, pow(t, 0.55));

    vec3 box0 = normalize(vec3(0.15, 0.85, 0.35));
    col += vec3(1.0, 0.98, 0.95) * pow(saturate(dot(dir, box0)), 25.0) * 4.5;

    vec3 box1 = normalize(vec3(-0.55, 0.65, 0.50));
    col += vec3(0.95, 0.98, 1.0) * pow(saturate(dot(dir, box1)), 35.0) * 3.5;

    col += vec3(0.20, 0.25, 0.30) * (1.0 - abs(dir.y)) * 0.25;
    return col;
}

// ------------------------------------------------------------
// Normal mapping via derivatives
// ------------------------------------------------------------
vec3 sampleNormalMapTS(int normalTex, vec2 uv)
{
    vec3 n = texture(uTextures[normalTex], uv).xyz * 2.0 - 1.0;
    return normalize(n);
}

mat3 tbnFromDerivatives(vec3 P, vec3 N, vec2 uv)
{
    vec3 dpdx = dFdx(P);
    vec3 dpdy = dFdy(P);
    vec2 dtdx = dFdx(uv);
    vec2 dtdy = dFdy(uv);

    vec3 T = dpdx * dtdy.y - dpdy * dtdx.y;
    vec3 B = dpdy * dtdx.x - dpdx * dtdy.x;

    T = normalize(T - N * dot(N, T));
    B = normalize(cross(N, T));
    return mat3(T, B, N);
}

// ============================================================
// Main
// ============================================================

void main()
{
    // -----------------------------
    // Material fetch
    // -----------------------------
    int matCount = int(materials.length());
    int id = (matCount > 0) ? clamp(vMaterialId, 0, matCount - 1) : 0;
    GpuMaterial mat = (matCount > 0) ? materials[id] : materials[0];

    // Base color
    vec3 albedo = mat.baseColor;
    if (mat.baseColorTexture >= 0)
        albedo *= texture(uTextures[mat.baseColorTexture], vUv).rgb;

    // View-space vectors
    vec3 N = normalize(nrm);
    vec3 V = normalize(-pos);

    if (mat.normalTexture >= 0)
    {
        mat3 TBN = tbnFromDerivatives(pos, N, vUv);
        N = normalize(TBN * sampleNormalMapTS(mat.normalTexture, vUv));
    }

    // Material params
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float metallic  = clamp(mat.metallic,  0.0,  1.0);
    float ao        = 1.0;

    if (mat.mraoTexture >= 0)
    {
        vec3 mrao = texture(uTextures[mat.mraoTexture], vUv).rgb;
        ao        = clamp(mrao.r, 0.0, 1.0);
        roughness = clamp(roughness * mrao.g, 0.04, 1.0);
        metallic  = clamp(metallic  * mrao.b, 0.0,  1.0);
    }

    float alpha = roughness * roughness;

    float ior = max(mat.ior, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08));
    vec3  F0  = mix(F0d, albedo, metallic);

    // =========================================================
    // Direct lighting (uses GpuLightsUBO)
    // =========================================================
    vec3 direct = vec3(0.0);

    uint lightCount = uLights.info.x;
    for (uint i = 0u; i < lightCount; ++i)
    {
        vec3 L = normalize(uLights.lights[i].dir_range.xyz); // surface -> light
        vec3 H = normalize(V + L);

        float NdotL = saturate(dot(N, L));
        float NdotV = saturate(dot(N, V));
        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0 && NdotV > 0.0)
        {
            vec3  F = fresnelSchlick(VdotH, F0);
            float D = D_GGX(NdotH, alpha);

            float r = roughness + 1.0;
            float k = (r * r) / 8.0;
            float G = G_Smith(NdotV, NdotL, k);

            vec3  specBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-7);
            vec3  kd       = (vec3(1.0) - F) * (1.0 - metallic);
            vec3  diffBRDF = kd * albedo / 3.14159265;

            vec3 radiance = uLights.lights[i].color_intensity.rgb *
                            uLights.lights[i].color_intensity.a;

            direct += (diffBRDF + specBRDF) * radiance * NdotL;
        }
    }

    // =========================================================
    // Indirect (unchanged)
    // =========================================================
    vec3 hemi = studioEnv(N);
    vec3 kdI  = (vec3(1.0) - fresnelSchlick(dot(N, V), F0)) * (1.0 - metallic);
    vec3 diffIBL = kdI * albedo * hemi * 0.25 * ao;

    vec3 R = reflect(-V, N);
    vec3 specIBL = studioEnv(R) * fresnelSchlick(dot(N, V), F0) *
                   (0.15 + 0.35 * (1.0 - roughness));

    // Ambient from UBO
    vec3 ambient = uLights.ambient.rgb * uLights.ambient.a * albedo;

    // Emissive
    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
    if (mat.emissiveTexture >= 0)
        emissive *= texture(uTextures[mat.emissiveTexture], vUv).rgb;

    vec3 color = direct + ambient + diffIBL + specIBL + emissive;
    fragColor  = vec4(color, mat.opacity);
}
