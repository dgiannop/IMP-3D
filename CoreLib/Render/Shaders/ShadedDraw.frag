//==============================================================
// ShadedDraw.frag  (WORLD-space shading; WORLD-space lights)
// Production-ready defaults:
// - Swapchain/target is SRGB: DO NOT gamma-encode here.
// - Camera-like exposure control using Lights.exposure.
// - WORLD-space light conventions match RT.
//==============================================================
#version 450

layout(location = 0) in vec3 posW;             // world-space position
layout(location = 1) in vec3 nrmW;             // world-space normal
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in int vMaterialId;

layout(location = 0) out vec4 fragColor;

// ============================================================
// Camera UBO (world camPos)
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
    vec3  baseColor;           // linear
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
    int emissiveTexture;       // sRGB if desired
};

layout(std430, set = 1, binding = 0) readonly buffer MaterialBuffer
{
    GpuMaterial materials[];
};

const int kMaxTextureCount = 512;
layout(set = 1, binding = 1) uniform sampler2D uTextures[kMaxTextureCount];

// ============================================================
// Lights (WORLD space; matches C++ GpuLight)
// ============================================================
struct GpuLight
{
    vec3 position;   // WORLD position for point/spot, unused for directional
    uint type;       // 0 = Directional, 1 = Point, 2 = Spot

    vec3 direction;  // WORLD forward direction for directional/spot
    float range;     // directional: softness radius (radians), point/spot: range (WORLD units)

    vec3 color;      // linear RGB color
    float intensity; // light strength

    // x = innerCos, y = outerCos, z = RT soft-shadow radius (point/spot), w reserved
    vec4 spot_params;
};

layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uint  count;          // number of active lights
    uint  pad0;
    uint  pad1;
    uint  pad2;
    vec3  ambient;        // ambient fill color (linear)
    float exposure;       // exposure scalar or UI value
    GpuLight lights[64];
} Lights;

// ============================================================
// Output controls
// ============================================================
// Swapchain is SRGB: keep gamma encode OFF.
const bool ENABLE_GAMMA_ENCODE = false;
const bool ENABLE_TONEMAP      = true;

// Use Lights.exposure as input to exposure mapping.
const bool  USE_UBO_EXPOSURE   = true;
const float FIXED_EXPOSURE     = 1.0;

// Optional ambient fill from Lights.ambient
const bool USE_AMBIENT_FILL    = true;

// Exposure mapping (UI 0..1 -> EV)
const float EXPOSURE_EV_MIN    = -3.0;
const float EXPOSURE_EV_MAX    =  3.0;

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
// Modest studio environment (WORLD +Y up) for viewport IBL
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
// Light evaluation (WORLD)
// ------------------------------------------------------------
const uint GPU_LIGHT_DIRECTIONAL = 0u;
const uint GPU_LIGHT_POINT       = 1u;
const uint GPU_LIGHT_SPOT        = 2u;

float smoothRangeWindow(float dist, float range)
{
    if (range <= 0.0) return 1.0;
    float x = saturate(1.0 - dist / range);
    // Smoothstep-like window to zero at range, less aggressive than x*x.
    return x * x * (3.0 - 2.0 * x);
}

void evalLight(in GpuLight Ld, in vec3 Pworld, out vec3 Lworld, out float atten)
{
    uint lt = Ld.type;
    atten   = 1.0;

    if (lt == GPU_LIGHT_DIRECTIONAL)
    {
        // Directional lights: direction is "forward"; surface->light is -direction.
        Lworld = normalize(-Ld.direction);
        return;
    }

    // Point or spot: position is WORLD space light position.
    vec3  toLight = Ld.position - Pworld; // surface->light vector
    float dist2   = max(dot(toLight, toLight), 1e-6);
    float dist    = sqrt(dist2);

    Lworld = toLight / dist;

    // Inverse-square attenuation
    atten = 1.0 / dist2;

    // Gentle range window
    atten *= smoothRangeWindow(dist, Ld.range);

    // Spotlight cone
    if (lt == GPU_LIGHT_SPOT)
    {
        vec3 spotFwdW = normalize(Ld.direction);

        // Light-to-surface direction
        vec3  lightToSurfaceW = normalize(Pworld - Ld.position);
        float cosAn           = dot(spotFwdW, lightToSurfaceW);

        float innerC = Ld.spot_params.x; // cos(inner)
        float outerC = Ld.spot_params.y; // cos(outer)

        float coneV = (innerC > outerC)
                    ? saturate((cosAn - outerC) / max(innerC - outerC, 1e-5))
                    : step(outerC, cosAn);

        // No extra squaring: keeps spots punchy while still smooth.
        atten *= coneV;
    }
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
        // If the image was created as *_SRGB, sampling returns linear automatically.
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

    uint lightCount = min(Lights.count, 64u);

    for (uint i = 0u; i < lightCount; ++i)
    {
        vec3  L;
        float atten;
        evalLight(Lights.lights[i], posW, L, atten);

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

        const float LIGHT_INTENSITY_SCALE = 5.0; // 2..10

        vec3 radiance = Lights.lights[i].color *
                        (Lights.lights[i].intensity * LIGHT_INTENSITY_SCALE * atten);

        direct += (diff + spec) * radiance * NdotL;
    }

    // --------------------------------------------------------
    // Indirect (viewport IBL) - conservative
    // --------------------------------------------------------
    float hasSceneLights = (Lights.count > 1u) ? 1.0 : 0.0;
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

    vec3 ambientFill = vec3(0.0);
    if (USE_AMBIENT_FILL)
        ambientFill = Lights.ambient * albedo * ao;

    vec3 colorLinear = direct + diffIBL + specIBL + floorFill + emissive + ambientFill;

    // --------------------------------------------------------
    // Exposure + tonemap (camera-like)
    // --------------------------------------------------------
    float exposure = FIXED_EXPOSURE;

    if (USE_UBO_EXPOSURE)
    {
        // Interpret Lights.exposure as a 0..1 control, like before.
        float t = saturate(Lights.exposure);
        float exposureEV = mix(EXPOSURE_EV_MIN, EXPOSURE_EV_MAX, t);
        exposure = exp2(exposureEV);
    }

    vec3 outRgb = colorLinear * exposure;

    if (ENABLE_TONEMAP)
        outRgb = tonemapACES(outRgb);
    else
        outRgb = clamp(outRgb, vec3(0.0), vec3(1.0));

    // Swapchain is SRGB: do NOT gamma encode here.
    if (ENABLE_GAMMA_ENCODE)
        outRgb = gammaEncode(outRgb);

    fragColor = vec4(outRgb, mat.opacity);
}
