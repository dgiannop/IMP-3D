//==============================================================
// ShadedDraw.frag  (WORLD-space shading to match SOLID world mode)
// - Assumes swapchain/target is SRGB: DO NOT gamma-encode here.
// - Uses a modest "studio" environment + conservative IBL energy.
// - Directional lights are WORLD-space (dir_range.xyz = forward world).
// - Point/spot lights are TEMPORARILY treated as VIEW-space (from your current C++ builder),
//   and converted to WORLD for BRDF evaluation.
//==============================================================
#version 450

// Match ShadedDraw.vert (WORLD outputs)
layout(location = 0) in vec3 posW;             // world-space position
layout(location = 1) in vec3 nrmW;             // world-space normal
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in int vMaterialId;

layout(location = 0) out vec4 fragColor;

// ============================================================
// Camera UBO (needed for camPos + view/invView conversions)
// ============================================================
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    mat4 viewProj;

    mat4 invProj;
    mat4 invView;
    mat4 invViewProj;

    vec4 camPos;     // world-space camera position
    vec4 viewport;
    vec4 clearColor;
} uCamera;

// ============================================================
// Materials (shared layout with SolidDraw.frag)
// ============================================================
struct GpuMaterial
{
    vec3  baseColor;           // EXPECTED LINEAR. If authored sRGB, convert on CPU.
    float opacity;

    vec3  emissiveColor;       // linear
    float emissiveIntensity;

    float roughness;
    float metallic;
    float ior;
    float pad0;

    int baseColorTexture;      // sRGB texture recommended
    int normalTexture;         // linear
    int mraoTexture;           // linear (R=AO, G=rough, B=metal)
    int emissiveTexture;       // sRGB if you want artist-friendly emissive colors
};

layout(std430, set = 1, binding = 0) readonly buffer MaterialBuffer
{
    GpuMaterial materials[];
};

const int kMaxTextureCount = 512;
layout(set = 1, binding = 1) uniform sampler2D uTextures[kMaxTextureCount];

// ============================================================
// Lights (MIGRATION STATE)
// ============================================================
// Directional:
//   - dir_range.xyz = light forward direction in WORLD space
//   - L_world = -dir_range.xyz
//
// Point/Spot (TEMP, from current C++ builder):
//   - pos_type.xyz  = position in VIEW space
//   - dir_range.xyz = spot forward direction in VIEW space
//   - dir_range.w   = range
//   - pos_type.w    = type
struct GpuLight
{
    vec4 pos_type;        // xyz = position (view) for point/spot, w = type
    vec4 dir_range;       // xyz = forward dir (world for directional, view for spot), w = range
    vec4 color_intensity; // rgb = color (linear), a = intensity
    vec4 spot_params;     // x = innerCos, y = outerCos
};

layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uvec4    info;        // x = lightCount
    vec4     ambient;     // a = exposure if USE_UBO_EXPOSURE
    GpuLight lights[64];
} uLights;

// ============================================================
// Output controls
// ============================================================
const bool  ENABLE_GAMMA_ENCODE = false; // keep false for VK_FORMAT_*_SRGB
const bool  ENABLE_TONEMAP      = true;
const bool  USE_UBO_EXPOSURE    = false;
const float FIXED_EXPOSURE      = 1.0;

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
// Modest studio environment (viewport IBL) - LOWER ENERGY
// NOTE: now interpreted in WORLD space; +Y is WORLD up.
// ------------------------------------------------------------
vec3 studioEnv(vec3 dir)
{
    dir = normalize(dir);

    float t = saturate(dir.y * 0.5 + 0.5);

    vec3 top    = vec3(1.0, 1.0, 1.05) * 0.75;
    vec3 bottom = vec3(0.03, 0.03, 0.035) * 0.55;

    vec3 col = mix(bottom, top, pow(t, 0.65));

    vec3 box0 = normalize(vec3(0.15, 0.85, 0.35));
    col += vec3(1.0, 0.98, 0.95) * pow(saturate(dot(dir, box0)), 25.0) * 1.5;

    vec3 box1 = normalize(vec3(-0.55, 0.65, 0.50));
    col += vec3(0.95, 0.98, 1.0) * pow(saturate(dot(dir, box1)), 35.0) * 1.1;

    col += vec3(0.20, 0.25, 0.30) * (1.0 - abs(dir.y)) * 0.15;
    return col;
}

// ------------------------------------------------------------
// Normal mapping (derivatives)
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

// ------------------------------------------------------------
// Light evaluation
//   - Directional: WORLD
//   - Point/Spot: VIEW -> converted to WORLD direction for BRDF
// ------------------------------------------------------------
const uint GPU_LIGHT_DIRECTIONAL = 0u;
const uint GPU_LIGHT_POINT       = 1u;
const uint GPU_LIGHT_SPOT        = 2u;

void evalLight(in GpuLight Ld, in vec3 Pworld, out vec3 Lworld, out float atten)
{
    uint lt = uint(Ld.pos_type.w + 0.5);
    atten   = 1.0;

    if (lt == GPU_LIGHT_DIRECTIONAL)
    {
        // WORLD: dir_range.xyz is forward direction the light points toward.
        Lworld = normalize(-Ld.dir_range.xyz); // surface->light in WORLD
        return;
    }

    // TEMP: point/spot are provided in VIEW space by current C++ builder
    vec3 Pview = (uCamera.view * vec4(Pworld, 1.0)).xyz;

    vec3  toLight = Ld.pos_type.xyz - Pview;
    float dist2   = max(dot(toLight, toLight), 1e-6);
    float dist    = sqrt(dist2);

    vec3 Lview = toLight / dist;

    // Inverse-square attenuation
    atten = 1.0 / dist2;

    float range = Ld.dir_range.w;
    if (range > 0.0)
    {
        float x = saturate(1.0 - dist / range);
        atten *= x * x;
    }

    if (lt == GPU_LIGHT_SPOT)
    {
        vec3  spotDirV = normalize(Ld.dir_range.xyz);     // view-space forward
        float cd       = dot(normalize(-spotDirV), Lview);

        float innerC = Ld.spot_params.x;
        float outerC = Ld.spot_params.y;

        float s = (innerC > outerC)
                ? saturate((cd - outerC) / (innerC - outerC))
                : step(outerC, cd);

        atten *= s;
    }

    // Convert L to WORLD so BRDF stays world-space
    Lworld = normalize((uCamera.invView * vec4(Lview, 0.0)).xyz);
}

// ------------------------------------------------------------
// Tonemap (ACES fitted)
// ------------------------------------------------------------
vec3 tonemapACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 gammaEncode(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

// ============================================================
// Main
// ============================================================
void main()
{
    int matCount = int(materials.length());
    int id       = (matCount > 0) ? clamp(vMaterialId, 0, matCount - 1) : 0;
    GpuMaterial mat = (matCount > 0) ? materials[id] : materials[0];

    vec3 albedo = mat.baseColor;

    if (mat.baseColorTexture >= 0)
    {
        // If the texture image was created as *_SRGB, sampling returns linear automatically.
        albedo *= texture(uTextures[mat.baseColorTexture], vUv).rgb;
    }

    vec3 N = normalize(nrmW);
    vec3 V = normalize(uCamera.camPos.xyz - posW);

    if (mat.normalTexture >= 0)
    {
        mat3 TBN = tbnFromDerivatives(posW, N, vUv);
        N = normalize(TBN * sampleNormalMapTS(mat.normalTexture, vUv));
    }

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

    // --------------------------------------------------------
    // Direct lighting
    // --------------------------------------------------------
    vec3 direct = vec3(0.0);

    uint lightCount = min(uLights.info.x, 64u);

    for (uint i = 0u; i < lightCount; ++i)
    {
        vec3  L;
        float atten;
        evalLight(uLights.lights[i], posW, L, atten);

        float NdotL = saturate(dot(N, L));
        float NdotV = saturate(dot(N, V));
        if (NdotL <= 0.0 || NdotV <= 0.0)
            continue;

        vec3  H     = normalize(V + L);
        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));

        vec3  F = fresnelSchlick(VdotH, F0);
        float D = D_GGX(NdotH, alpha);

        float r = roughness + 1.0;
        float k = (r * r) / 8.0;
        float G = G_Smith(NdotV, NdotL, k);

        vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-7);
        vec3 kd   = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diff = kd * albedo / 3.14159265;

        vec3 radiance = uLights.lights[i].color_intensity.rgb *
                        (uLights.lights[i].color_intensity.a * atten);

        radiance = min(radiance, vec3(25.0));

        direct += (diff + spec) * radiance * NdotL;
    }

    // --------------------------------------------------------
    // Indirect (viewport IBL) - conservative
    // --------------------------------------------------------
    float hasSceneLights = (uLights.info.x > 1u) ? 1.0 : 0.0;
    float iblScale = mix(1.0, 0.20, hasSceneLights);

    vec3  Fv   = fresnelSchlick(saturate(dot(N, V)), F0);
    vec3  kdI  = (vec3(1.0) - Fv) * (1.0 - metallic);

    vec3 diffIBL = kdI * albedo * studioEnv(N) * 0.10 * ao * iblScale;

    vec3 R = reflect(-V, N);

    vec3 specIBL = studioEnv(R) *
                   Fv *
                   (0.05 + 0.25 * (1.0 - roughness)) * iblScale;

    vec3 floorFill = albedo * 0.004;

    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
    if (mat.emissiveTexture >= 0)
        emissive *= texture(uTextures[mat.emissiveTexture], vUv).rgb;

    vec3 colorLinear = direct + diffIBL + specIBL + floorFill + emissive;

    float exposure = USE_UBO_EXPOSURE ? max(uLights.ambient.a, 0.0)
                                      : FIXED_EXPOSURE;

    if (USE_UBO_EXPOSURE == false && uLights.info.x <= 1u)
        exposure *= 1.10;

    vec3 outRgb = colorLinear * exposure;

    if (ENABLE_TONEMAP)
        outRgb = tonemapACES(outRgb);
    else
        outRgb = clamp(outRgb, vec3(0.0), vec3(1.0));

    if (ENABLE_GAMMA_ENCODE)
        outRgb = gammaEncode(outRgb);

    fragColor = vec4(outRgb, mat.opacity);
}
