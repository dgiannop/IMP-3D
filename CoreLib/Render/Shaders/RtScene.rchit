//==============================================================
// RtScene.rchit
// PBR + studio IBL + exposure EV mapping + WORLD-space lights
// + Shadows (dir soft, point/spot soft via spot_params.z)
// + OPTIONAL: single-bounce perfect reflections (compile flag)
//
// SBT ASSUMPTIONS (you must match C++ SBT records):
//   HitGroup[0] = Primary shading (this shader)
//   HitGroup[1] = Shadow occlusion hit (RtShadow.rchit OR RtShadow.rahit)
//   Miss[0]     = Primary miss (RtScene.rmiss)
//   Miss[1]     = Shadow miss (RtShadow.rmiss)  -> leaves occFlag = 0
//
// PAYLOAD CONVENTION:
//   - payload (location=0)  = vec4 color/aux
//       * During RT recursion: payload.w carries "depth" as int.
//       * RayGen must set payload = vec4(0,0,0, 0) for primary rays.
//       * This shader writes:
//           depth==0 : payload.w = 1.0  (alpha for present pass)
//           depth>0  : payload.w = depth (for nested rays)
//   - occFlag (location=1)  = uint shadow-occlusion flag for shadow rays.
//
// LIGHT UBO LAYOUT (must match C++ GpuLightsUBO):
//   layout(set = 0, binding = 1, std140) uniform LightsUBO
//   {
//       uint  count;
//       uint  pad0;
//       uint  pad1;
//       uint  pad2;
//       vec3  ambient;
//       float exposure;
//       GpuLight lights[64];
//   } Lights;
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) rayPayloadInEXT vec4 payload;
layout(location = 1) rayPayloadEXT   uint occFlag; // used only for shadow rays

hitAttributeEXT vec2 hitAttr;

// ------------------------------------------------------------
// Controls (compile-time switches)
// ------------------------------------------------------------

// Shadows
#define ENABLE_SHADOWS        1
#define ENABLE_DIR_SOFT       1
#define DIR_SHADOW_SAMPLES    4       // 1,2,4,8...
#define SHADOW_BIAS_MIN       1e-3    // world
#define SHADOW_BIAS_SLOPE     1e-4    // world * tHit

// Tonemap / exposure
#define ENABLE_TONEMAP        1
#define USE_UBO_EXPOSURE      1
#define FIXED_EXPOSURE        1.0

// Exposure mapping (UI 0..1 -> EV), MUST match raster
#define EXPOSURE_EV_MIN      (-3.0)
#define EXPOSURE_EV_MAX       (3.0)

// Ambient fill (must match raster look)
#define USE_AMBIENT_FILL      1

// Optional: 1-bounce perfect reflections (no noise, no GI)
#define ENABLE_SIMPLE_REFLECTIONS  1
#define MAX_RT_DEPTH               2    // 1=primary only, 2=primary + 1 bounce
#define REFLECTION_RAY_MIN_T       0.001

// Light scale (must match raster)
#define LIGHT_INTENSITY_SCALE      5.0

// ------------------------------------------------------------
// Buffer references (device address)
// ------------------------------------------------------------
layout(buffer_reference, scalar) readonly buffer PosBuf { vec4 pos4[]; };
layout(buffer_reference, scalar) readonly buffer IdxBuf { uint ind4[]; }; // triCnt*4 (padded)
layout(buffer_reference, scalar) readonly buffer NrmBuf { vec4 nrm4[]; }; // triCnt*3
layout(buffer_reference, scalar) readonly buffer UvBuf  { vec4 uvv4[]; }; // triCnt*3
layout(buffer_reference, scalar) readonly buffer MatBuf { uint mat1[]; }; // triCnt

// ------------------------------------------------------------
// TLAS + per-instance data
// ------------------------------------------------------------
layout(set = 2, binding = 2) uniform accelerationStructureEXT tlasTop;

struct InstDat
{
    uint64_t posAdr;
    uint64_t idxAdr;
    uint64_t nrmAdr;
    uint64_t uvvAdr;
    uint64_t matAdr;
    uint     triCnt;
    uint     pad0;
    uint     pad1;
    uint     pad2;
};

layout(set = 2, binding = 3, std430) readonly buffer InstBuf
{
    InstDat insts[];
} instBuf;

// ------------------------------------------------------------
// Camera UBO (WORLD camPos)
// ------------------------------------------------------------
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    mat4 viewProj;

    mat4 invProj;
    mat4 invView;
    mat4 invViewProj;

    vec4 camPos;     // WORLD
    vec4 viewport;
    vec4 clearColor;
} uCamera;

// ------------------------------------------------------------
// Lights UBO (WORLD space; matches raster and C++)
// ------------------------------------------------------------
struct GpuLight
{
    vec4 pos_type;        // xyz = pos (WORLD) for point/spot, w = type
    vec4 dir_range;       // xyz = forward dir (WORLD), w = range OR angular radius (dir)
    vec4 color_intensity; // rgb = color (linear), a = intensity
    vec4 spot_params;     // x = innerCos, y = outerCos, z = point/spot soft ang radius (radians)
};

layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uint  count;
    uint  pad0;
    uint  pad1;
    uint  pad2;
    vec3  ambient;   // ambient fill color (linear)
    float exposure;  // UI 0..1 (mapped to EV in shader)
    GpuLight lights[64];
} Lights;

// ------------------------------------------------------------
// Materials + textures (must match C++/raster layout)
// ------------------------------------------------------------
struct GpuMat
{
    vec3  baseCol;
    float opacIt;

    vec3  emisCol;
    float emisIt;

    float roughIt;
    float metalIt;
    float iorVal;
    float padIt;

    int baseTex;
    int normTex;
    int mraoTex;
    int emisTex;
};

layout(std430, set = 1, binding = 0) readonly buffer MatSSB
{
    GpuMat mats[];
} matSsb;

const int kMaxTex = 512;
layout(set = 1, binding = 1) uniform sampler2D tex2d[kMaxTex];

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(1.0 - saturate(cosTheta), 5.0);
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

vec3 tonemapACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Modest studio env (same as raster ShadedDraw.frag)
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

// Normal from object->world (buffers usually object space; required if instances move)
vec3 toWorldNormal(vec3 nObj)
{
    mat3 objToWorld3 = mat3(gl_ObjectToWorldEXT);
    mat3 nrmMat      = transpose(inverse(objToWorld3));
    return normalize(nrmMat * nObj);
}

// ------------------------------------------------------------
// Light evaluation (MATCH raster ShadedDraw.frag)
// ------------------------------------------------------------
const uint GPU_LIGHT_DIRECTIONAL = 0u;
const uint GPU_LIGHT_POINT       = 1u;
const uint GPU_LIGHT_SPOT        = 2u;

float smoothRangeWindow(float dist, float range)
{
    if (range <= 0.0) return 1.0;
    float x = saturate(1.0 - dist / range);
    return x * x * (3.0 - 2.0 * x);
}

void evalLight(in GpuLight Ld, in vec3 Pworld, out vec3 Lworld, out float atten)
{
    uint lt = uint(Ld.pos_type.w + 0.5);
    atten   = 1.0;

    if (lt == GPU_LIGHT_DIRECTIONAL)
    {
        Lworld = normalize(-Ld.dir_range.xyz); // surface->light
        return;
    }

    vec3  toLight = Ld.pos_type.xyz - Pworld;
    float dist2   = max(dot(toLight, toLight), 1e-6);
    float dist    = sqrt(dist2);

    Lworld = toLight / dist;

    atten = 1.0 / dist2;
    atten *= smoothRangeWindow(dist, Ld.dir_range.w);

    if (lt == GPU_LIGHT_SPOT)
    {
        vec3 spotFwdW = normalize(Ld.dir_range.xyz);

        vec3  lightToSurfaceW = normalize(Pworld - Ld.pos_type.xyz);
        float cosAn           = dot(spotFwdW, lightToSurfaceW);

        float innerC = Ld.spot_params.x; // cos(inner)
        float outerC = Ld.spot_params.y; // cos(outer)

        float coneV = (innerC > outerC)
                    ? saturate((cosAn - outerC) / max(innerC - outerC, 1e-5))
                    : step(outerC, cosAn);

        atten *= coneV; // NOTE: no extra squaring (matches raster)
    }
}

// ------------------------------------------------------------
// Shadows
// ------------------------------------------------------------
float hash13(uvec3 key3)
{
    key3 = (key3 ^ (key3 >> 16u)) * 0x7feb352du;
    key3 = (key3 ^ (key3 >> 15u)) * 0x846ca68bu;
    key3 = (key3 ^ (key3 >> 16u));
    uint mixV = key3.x ^ key3.y ^ key3.z;
    return float(mixV) / 4294967295.0;
}

vec3 coneSample(vec3 dirIn, float angRad, vec2 rnd2)
{
    float cosA  = cos(angRad);
    float cosT  = mix(1.0, cosA, rnd2.x);
    float sinT  = sqrt(max(1.0 - cosT * cosT, 0.0));
    float phiV  = 2.0 * 3.14159265 * rnd2.y;

    vec3 axisW  = normalize(dirIn);
    vec3 axisU  = normalize(abs(axisW.z) < 0.999 ? cross(axisW, vec3(0, 0, 1)) : cross(axisW, vec3(0, 1, 0)));
    vec3 axisV  = cross(axisW, axisU);

    vec3 dirOut = axisU * (cos(phiV) * sinT) +
                  axisV * (sin(phiV) * sinT) +
                  axisW * cosT;

    return normalize(dirOut);
}

// Shadow visibility for a ray: returns 1 if unoccluded else 0
float traceShadow(vec3 orgW, vec3 dirW, float tMax)
{
#if !ENABLE_SHADOWS
    return 1.0;
#endif

    occFlag = 0u;

    traceRayEXT(
        tlasTop,
        gl_RayFlagsTerminateOnFirstHitEXT |
        gl_RayFlagsCullBackFacingTrianglesEXT,
        0xFF,
        1, 1, 1,          // sbtRecordOffset=1, sbtRecordStride=1, missIndex=1 (shadow)
        orgW,
        0.001,
        dirW,
        tMax,
        1
    );

    return (occFlag == 0u) ? 1.0 : 0.0;
}

float shadowDirectional(vec3 P, vec3 N, vec3 Ldir, float angRad)
{
#if !ENABLE_SHADOWS
    return 1.0;
#endif

    float eps = max(SHADOW_BIAS_MIN, SHADOW_BIAS_SLOPE * gl_HitTEXT);
    vec3  org = P + N * eps;

#if !ENABLE_DIR_SOFT
    return traceShadow(org, normalize(Ldir), 1e30);
#else
    if (angRad <= 0.0 || DIR_SHADOW_SAMPLES <= 1)
        return traceShadow(org, normalize(Ldir), 1e30);

    float sum = 0.0;
    for (int s = 0; s < DIR_SHADOW_SAMPLES; ++s)
    {
        uvec3 key3 = uvec3(gl_LaunchIDEXT.xy, uint(s));
        float r0   = hash13(key3);
        float r1   = hash13(key3 ^ uvec3(12345u, 67890u, 424242u));

        vec3 d = coneSample(Ldir, angRad, vec2(r0, r1));
        sum += traceShadow(org, d, 1e30);
    }

    return sum / float(DIR_SHADOW_SAMPLES);
#endif
}

float shadowPointOrSpotSoft(vec3 P, vec3 N, vec3 Ldir, float distToLight, float angRad)
{
#if !ENABLE_SHADOWS
    return 1.0;
#endif

    float eps = max(SHADOW_BIAS_MIN, SHADOW_BIAS_SLOPE * gl_HitTEXT);
    vec3  org = P + N * eps;

    float tMax = max(distToLight - 0.01, 0.01);

    // Hard shadow if no angular radius
    if (angRad <= 0.0)
        return traceShadow(org, normalize(Ldir), tMax);

    // Soft shadow: sample a cone around the light direction.
    const int sampCt = 4; // tune: 4/8/16
    float     sum    = 0.0;

    for (int s = 0; s < sampCt; ++s)
    {
        uvec3 key3 = uvec3(gl_LaunchIDEXT.xy, uint(s));
        float r0   = hash13(key3);
        float r1   = hash13(key3 ^ uvec3(12345u, 67890u, 424242u));

        vec3 d = coneSample(normalize(Ldir), angRad, vec2(r0, r1));
        sum += traceShadow(org, d, tMax);
    }

    return sum / float(sampCt);
}

// ------------------------------------------------------------
// Optional: simple reflections (single bounce)
// ------------------------------------------------------------
#if ENABLE_SIMPLE_REFLECTIONS
vec3 traceReflection(vec3 orgW, vec3 dirW, int nextDepth)
{
    // Save caller payload (we are reusing the same payload variable)
    vec4 saved = payload;

    // Nested ray starts with black, and carries depth in .w
    payload = vec4(0.0, 0.0, 0.0, float(nextDepth));

    // Trace using the SAME primary hit group + primary miss.
    traceRayEXT(
        tlasTop,
        gl_RayFlagsCullBackFacingTrianglesEXT | gl_RayFlagsOpaqueEXT,
        0xFF,
        0, 1, 0,                 // sbtRecordOffset=0, stride=1, missIndex=0
        orgW,
        REFLECTION_RAY_MIN_T,
        dirW,
        1e30,
        0
    );

    vec3 col = payload.rgb;

    // Restore caller payload
    payload = saved;
    return col;
}
#endif

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
void main()
{
    // Depth encoded in payload.w during RT recursion (see file header note)
    int depth = int(payload.w + 0.5);

    const uint instId = gl_InstanceCustomIndexEXT;

    if (instId >= uint(instBuf.insts.length()))
    {
        payload = vec4(1.0, 0.0, 1.0, (depth == 0) ? 1.0 : float(depth));
        return;
    }

    InstDat instDat = instBuf.insts[instId];

    if (instDat.posAdr == 0ul ||
        instDat.idxAdr == 0ul ||
        instDat.nrmAdr == 0ul ||
        instDat.uvvAdr == 0ul ||
        instDat.matAdr == 0ul ||
        instDat.triCnt == 0u)
    {
        payload = vec4(1.0, 0.0, 1.0, (depth == 0) ? 1.0 : float(depth));
        return;
    }

    const uint primId = gl_PrimitiveID;
    if (primId >= instDat.triCnt)
    {
        payload = vec4(1.0, 0.0, 1.0, (depth == 0) ? 1.0 : float(depth));
        return;
    }

    PosBuf posBuf = PosBuf(instDat.posAdr);
    IdxBuf idxBuf = IdxBuf(instDat.idxAdr);
    NrmBuf nrmBuf = NrmBuf(instDat.nrmAdr);
    UvBuf  uvvBuf = UvBuf(instDat.uvvAdr);
    MatBuf matBuf = MatBuf(instDat.matAdr);

    // WORLD hit position
    vec3 posW = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // Triangle indices
    const uint base4 = primId * 4u;
    const uint idxA  = idxBuf.ind4[base4 + 0u];
    const uint idxB  = idxBuf.ind4[base4 + 1u];
    const uint idxC  = idxBuf.ind4[base4 + 2u];

    vec3 posA = posBuf.pos4[idxA].xyz;
    vec3 posB = posBuf.pos4[idxB].xyz;
    vec3 posC = posBuf.pos4[idxC].xyz;

    // Barycentrics
    const float barB = hitAttr.x;
    const float barC = hitAttr.y;
    const float barA = 1.0 - barB - barC;

    // Per-tri normals/uvs (triCnt*3)
    const uint base3 = primId * 3u;

    vec3 nA = nrmBuf.nrm4[base3 + 0u].xyz;
    vec3 nB = nrmBuf.nrm4[base3 + 1u].xyz;
    vec3 nC = nrmBuf.nrm4[base3 + 2u].xyz;

    vec3 nObj = normalize(nA * barA + nB * barB + nC * barC);

    vec3 geoNObj = normalize(cross(posB - posA, posC - posA));
    if (dot(nObj, nObj) < 1e-20)
        nObj = geoNObj;

    vec3 N = toWorldNormal(nObj);

    // Backface handling (raster did gl_FrontFacing flip)
    const bool isBackface = (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT);
    if (isBackface)
        N = -N;

    // UVs
    vec2 uvA = uvvBuf.uvv4[base3 + 0u].xy;
    vec2 uvB = uvvBuf.uvv4[base3 + 1u].xy;
    vec2 uvC = uvvBuf.uvv4[base3 + 2u].xy;
    vec2 vUv = uvA * barA + uvB * barB + uvC * barC;

    // View vector (WORLD)
    vec3 V = normalize(uCamera.camPos.xyz - posW);

    // Material fetch
    const uint matRaw = matBuf.mat1[primId];
    const uint matCnt = uint(matSsb.mats.length());
    const uint matId  = (matCnt > 0u) ? min(matRaw, matCnt - 1u) : 0u;

    GpuMat mat = (matCnt > 0u)
        ? matSsb.mats[matId]
        : GpuMat(
            vec3(1, 0, 1), 1.0,
            vec3(0),       0.0,
            0.5, 0.0, 1.5, 0.0,
            -1, -1, -1, -1);

    vec3 albedo = mat.baseCol;
    if (mat.baseTex >= 0)
        albedo *= texture(tex2d[mat.baseTex], vUv).rgb;

    float roughness = clamp(mat.roughIt, 0.04, 1.0);
    float metallic  = clamp(mat.metalIt,  0.0,  1.0);
    float ao        = 1.0;

    if (mat.mraoTex >= 0)
    {
        vec3 mrao = texture(tex2d[mat.mraoTex], vUv).rgb;
        ao        = clamp(mrao.r, 0.0, 1.0);
        roughness = clamp(roughness * mrao.g, 0.04, 1.0);
        metallic  = clamp(metallic  * mrao.b, 0.0,  1.0);
    }

    float alpha = roughness * roughness;

    float ior = max(mat.iorVal, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08));
    vec3  F0  = mix(F0d, albedo, metallic);

    // --------------------------------------------------------
    // Direct lighting (+ shadows)
    // --------------------------------------------------------
    vec3 direct = vec3(0.0);

    uint lightCount = min(Lights.count, 64u);

    for (uint i = 0u; i < lightCount; ++i)
    {
        GpuLight Ld = Lights.lights[i];

        vec3  L;
        float atten;
        evalLight(Ld, posW, L, atten);

#if ENABLE_SHADOWS
        {
            uint lt = uint(Ld.pos_type.w + 0.5);

            if (lt == GPU_LIGHT_DIRECTIONAL)
            {
                float angRad = max(Ld.dir_range.w, 0.0);
                float vis    = shadowDirectional(posW, N, L, angRad);
                atten *= vis;
            }
            else
            {
                float distToLight = length(Ld.pos_type.xyz - posW);
                float angRad      = max(Ld.spot_params.z, 0.0); // point/spot angular radius (radians)
                float vis         = shadowPointOrSpotSoft(posW, N, L, distToLight, angRad);
                atten *= vis;
            }
        }
#endif

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

        vec3 radiance = Ld.color_intensity.rgb *
                        (Ld.color_intensity.a * LIGHT_INTENSITY_SCALE * atten);

        direct += (diff + spec) * radiance * NdotL;
    }

    // --------------------------------------------------------
    // Indirect (studio IBL) + ambient fill (match raster)
    // --------------------------------------------------------
    float hasSceneLights = (Lights.count > 1u) ? 1.0 : 0.0;
    float iblScale       = mix(1.0, 0.20, hasSceneLights);

    vec3  Fv  = fresnelSchlick(saturate(dot(N, V)), F0);
    vec3  kdI = (vec3(1.0) - Fv) * (1.0 - metallic);

    vec3 diffIBL = kdI * albedo * studioEnv(N) * 0.10 * ao * iblScale;

    vec3 R = reflect(-V, N);
    vec3 specIBL = studioEnv(R) *
                   Fv *
                   (0.05 + 0.25 * (1.0 - roughness)) * iblScale;

    vec3 floorFill = albedo * 0.004;

    vec3 emissive = mat.emisCol * mat.emisIt;
    if (mat.emisTex >= 0)
        emissive *= texture(tex2d[mat.emisTex], vUv).rgb;

    vec3 ambientFill = vec3(0.0);
#if USE_AMBIENT_FILL
    ambientFill = Lights.ambient * albedo * ao;
#endif

    vec3 colorLinear = direct + diffIBL + specIBL + floorFill + emissive + ambientFill;
    colorLinear = max(colorLinear, vec3(0.0));

    // --------------------------------------------------------
    // Optional: 1-bounce perfect reflections (no noise)
    // --------------------------------------------------------
#if ENABLE_SIMPLE_REFLECTIONS
    if (depth < (MAX_RT_DEPTH - 1))
    {
        // Only do reflections for smooth / metallic surfaces.
        float reflectStrength = max(metallic, 1.0 - roughness);
        reflectStrength       = saturate(reflectStrength);

        if (reflectStrength > 0.01)
        {
            vec3 I = normalize(gl_WorldRayDirectionEXT);
            vec3 Rdir = normalize(reflect(I, N));

            float eps = max(SHADOW_BIAS_MIN, SHADOW_BIAS_SLOPE * gl_HitTEXT);
            vec3  org = posW + N * eps;

            vec3 reflCol = traceReflection(org, Rdir, depth + 1);

            vec3 F = fresnelSchlick(saturate(dot(N, V)), F0);

            colorLinear =
                mix(colorLinear, reflCol, 0.35 * reflectStrength) * (1.0 - 0.15 * reflectStrength) +
                reflCol * (0.15 * reflectStrength) * F;
        }
    }
#endif

    // --------------------------------------------------------
    // Exposure + tonemap (MUST match raster ShadedDraw.frag)
    // --------------------------------------------------------
    float exposure = FIXED_EXPOSURE;

#if USE_UBO_EXPOSURE
    {
        float t          = saturate(Lights.exposure); // UI 0..1
        float exposureEV = mix(EXPOSURE_EV_MIN, EXPOSURE_EV_MAX, t);
        exposure         = exp2(exposureEV);
    }
#endif

    vec3 outRgb = colorLinear * exposure;

#if ENABLE_TONEMAP
    outRgb = tonemapACES(outRgb);
#else
    outRgb = clamp(outRgb, vec3(0.0), vec3(1.0));
#endif

    // Output:
    // - depth==0: store alpha=1 for your present path
    // - depth>0 : keep payload.w=depth for nested rays
    payload = vec4(outRgb, (depth == 0) ? 1.0 : float(depth));
}
