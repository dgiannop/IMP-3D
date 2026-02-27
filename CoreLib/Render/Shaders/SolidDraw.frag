//==============================================================
// SolidDraw.frag  (WORLD-space editor solid)
//==============================================================
#version 450

layout(location = 0) in vec3 posW;
layout(location = 1) in vec3 nrmW;
layout(location = 2) in vec2 vUv;
layout(location = 3) flat in int vMaterialId;

layout(location = 0) out vec4 fragColor;

const bool USE_TEXTURES_SOLID = true;

// Camera UBO (needed here for camPos)
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    mat4 viewProj;

    mat4 invProj;
    mat4 invView;
    mat4 invViewProj;

    vec4 camPos;     // world
    vec4 viewport;
    vec4 clearColor;
} uCamera;

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

// Must match the unified GpuLight used in RT / ShadedDraw
struct GpuLight
{
    vec4 pos_type;        // xyz = pos (WORLD) for point/spot, w = type
    vec4 dir_range;       // xyz = forward dir (WORLD), w = range OR angular radius (dir)
    vec4 color_intensity; // rgb = color, a = intensity
    vec4 spot_params;     // x = innerCos, y = outerCos, z = angular radius (radians)
};

// Unified LightsUBO layout (matches ShadedDraw + RT)
layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uint  count;          // number of active lights
    uint  pad0;
    uint  pad1;
    uint  pad2;
    vec3  ambient;        // ambient fill (unused here)
    float exposure;       // exposure scalar/UI (unused here)
    GpuLight lights[64];
} Lights;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  tonemapReinhard(vec3 x) { return x / (1.0 + x); }

void main()
{
    vec3 N = normalize(nrmW);

    const bool isBackface = !gl_FrontFacing;
    if (isBackface)
        N = -N;

    // View vector in world space (camera -> point)
    vec3 V = normalize(uCamera.camPos.xyz - posW);

    int matCount = int(materials.length());
    int id       = (matCount > 0) ? clamp(vMaterialId, 0, matCount - 1) : 0;

    vec3 base = vec3(0.8);
    if (matCount > 0)
    {
        GpuMaterial mat = materials[id];
        base = mat.baseColor;

        if (USE_TEXTURES_SOLID &&
            mat.baseColorTexture >= 0 &&
            mat.baseColorTexture < kMaxTextureCount)
        {
            base *= texture(uTextures[mat.baseColorTexture], vUv).rgb;
        }
    }

    // Headlight (fallback) in WORLD space
    vec3  L = normalize(vec3(0.25, 0.35, -0.90)); // surface->light (world)
    vec3  c = vec3(1.0);
    float I = 1.0;

    // If we have lights in the UBO, use light 0 as the "main" light
    if (Lights.count > 0u)
    {
        L = normalize(-Lights.lights[0].dir_range.xyz); // dir_range.xyz is forward; we want surface->light
        c = Lights.lights[0].color_intensity.rgb;
        I = Lights.lights[0].color_intensity.a;
    }

    float NdotL = saturate(dot(N, L));
    float diff  = pow(NdotL, 1.65);

    // Base fill
    vec3 lit = base * 0.06;

    // Simple hemisphere boost
    float hemi = saturate(dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5);
    lit *= mix(0.75, 1.0, hemi);

    // Main diffuse lobe
    lit += base * (c * (I * diff)) * 0.90;

    // Simple specular highlight
    vec3  H    = normalize(V + L);
    float spec = pow(saturate(dot(N, H)), 48.0) * 0.035;
    lit += (c * I) * spec;

    // Rim
    float rim = pow(1.0 - saturate(dot(N, V)), 3.0);
    lit += base * rim * 0.010;

    // Inner faces dimmer
    const float kInnerFaceDim = 0.35;
    if (isBackface)
    {
        vec3 baseLine = base * 0.06;
        lit = baseLine + (lit - baseLine) * kInnerFaceDim;
    }

    lit = tonemapReinhard(lit);
    fragColor = vec4(lit, 1.0);
}
