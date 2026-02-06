//==============================================================
// SolidDraw.frag  (Modeling Solid: two-sided + dim inner faces)
//==============================================================
#version 450

layout(location = 0) in vec3 pos; // view-space position
layout(location = 1) in vec3 nrm; // view-space normal
layout(location = 2) in vec2 vUv; // unused (kept to match vert)
layout(location = 3) flat in int vMaterialId;

layout(location = 0) out vec4 fragColor;

// ------------------------------
// Materials (baseColor only)
// ------------------------------
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

// ------------------------------
// Lights UBO (use ONLY light 0 = headlight)
// ------------------------------
struct GpuLight
{
    vec4 pos_type;
    vec4 dir_range;       // xyz = light forward dir (view), so L = -dir_range.xyz
    vec4 color_intensity; // rgb, a = intensity
    vec4 spot_params;
};

layout(std140, set = 0, binding = 1) uniform LightsUBO
{
    uvec4   info;
    vec4    ambient; // rgb ignored here, a ignored here
    GpuLight lights[64];
} uLights;

float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 tonemapReinhard(vec3 x) { return x / (1.0 + x); }

void main()
{
    vec3 N = normalize(nrm);
    vec3 V = normalize(-pos);

    // ----------------------------------------------------------
    // Two-sided "editor solid" lighting
    // ----------------------------------------------------------
    const bool isBackface = !gl_FrontFacing;
    if (isBackface)
        N = -N;

    // ----------------------------------------------------------
    // Material base color
    // ----------------------------------------------------------
    int matCount = int(materials.length());
    int id       = (matCount > 0) ? clamp(vMaterialId, 0, matCount - 1) : 0;
    vec3 base    = (matCount > 0) ? materials[id].baseColor : vec3(0.8);

    // ----------------------------------------------------------
    // Headlight (light 0) or fallback
    // ----------------------------------------------------------
    vec3  L = normalize(vec3(0.25, 0.35, 0.90));
    vec3  c = vec3(1.0);
    float I = 1.0;

    if (uLights.info.x > 0u)
    {
        L = normalize(-uLights.lights[0].dir_range.xyz);
        c = uLights.lights[0].color_intensity.rgb;
        I = uLights.lights[0].color_intensity.a;
    }

    float NdotL = saturate(dot(N, L));

    // Lambert (self shadowing)
    float diff = pow(NdotL, 1.65); // "Solid Contrast" knob.

    // Lower baseline so shapes don't wash out
    vec3 lit = base * 0.06;

    // Very subtle hemisphere shaping to keep the scene readable
    float hemi = saturate(dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5);
    lit *= mix(0.75, 1.0, hemi);

    // Diffuse
    lit += base * (c * (I * diff)) * 0.90;

    // Tiny spec kick (Small so it doesn't look "shaded mode")
    vec3  H    = normalize(V + L);
    float spec = pow(saturate(dot(N, H)), 48.0) * 0.035;
    lit += (c * I) * spec;

    // Rim: reduce a bit so it doesn't flatten the shadows
    float rim = pow(1.0 - saturate(dot(N, V)), 3.0);
    lit += base * rim * 0.010;

    // ----------------------------------------------------------
    // Dim interior faces so they read as "inside"
    // ----------------------------------------------------------
    // Tune this:
    //  - 0.60 subtle
    //  - 0.40 clear
    //  - 0.25 very obvious
    //  - 0.15 almost hidden
    const float kInnerFaceDim = 0.35;

    // Option 1 (simple): dim everything on backfaces
    // if (isBackface) lit *= kInnerFaceDim;

    // Option 2 (recommended): keep baseline, dim only the "lighting part"
    if (isBackface)
    {
        vec3 baseLine = base * 0.06;
        lit = baseLine + (lit - baseLine) * kInnerFaceDim;
    }

    lit = tonemapReinhard(lit);

    fragColor = vec4(lit, 1.0);
}
