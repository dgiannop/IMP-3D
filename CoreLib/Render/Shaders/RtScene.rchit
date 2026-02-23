//==============================================================
// RtScene.rchit  (FULL REPLACEMENT)
// PBR + studio IBL + exposure EV mapping + WORLD-space lights
// + Shadows (dir soft, point/spot hard by default)
//
// SBT ASSUMPTIONS (you must match your C++ SBT records):
//   HitGroup[0] = Primary shading (this shader)
//   HitGroup[1] = Shadow occlusion hit (RtShadow.rchit OR RtShadow.rahit)
//   Miss[0]     = Primary miss (RtScene.rmiss)
//   Miss[1]     = Shadow miss (RtShadow.rmiss)  -> leaves occFlag = 0
//
// If you don’t have alpha cutouts in RT yet, use RtShadow.rchit (closest-hit).
// If you DO have alpha cutouts, use RtShadow.rahit (any-hit) + closest-hit optional.
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) rayPayloadInEXT vec4 payload;
layout(location = 1) rayPayloadEXT   uint occFlag; // used only when tracing shadow rays from here

hitAttributeEXT vec2 hitAttr;

// ------------------------------------------------------------
// Controls
// ------------------------------------------------------------
const bool  ENABLE_SHADOWS      = true;
const bool  ENABLE_DIR_SOFT     = true;
const int   DIR_SHADOW_SAMPLES  = 4;      // 1,2,4,8...
const float SHADOW_BIAS_MIN     = 1e-3;   // world
const float SHADOW_BIAS_SLOPE   = 1e-4;   // world * tHit

const bool  ENABLE_TONEMAP      = true;
const bool  USE_UBO_EXPOSURE    = true;
const float FIXED_EXPOSURE      = 1.0;

// Exposure mapping (UI 0..1 -> EV), MUST match raster
const float EXPOSURE_EV_MIN     = -3.0;
const float EXPOSURE_EV_MAX     =  3.0;

// Studio IBL scale, MUST match raster if you want identical look
const bool  USE_AMBIENT_FILL    = true;

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
// Lights UBO (WORLD space; matches raster)
// ------------------------------------------------------------
struct GpuLight
{
    vec4 pos_type;        // xyz = pos (WORLD) for point/spot, w = type
    vec4 dir_range;       // xyz = forward dir (WORLD), w = range OR angular radius (dir)
    vec4 color_intensity; // rgb = color (linear), a = intensity
    vec4 spot_params;     // x = innerCos, y = outerCos
};

layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uvec4    info;    // x = lightCount
    vec4     ambient; // rgb ambient fill, a exposure UI scalar (0..1)
    GpuLight lights[64];
} uLights;

// ------------------------------------------------------------
// Materials + textures (match raster bindings)
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

        atten *= coneV; // NOTE: no extra squaring (matches your raster)
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
    if (!ENABLE_SHADOWS)
        return 1.0;

    occFlag = 0u;

    traceRayEXT(
        tlasTop,
        gl_RayFlagsTerminateOnFirstHitEXT |
        gl_RayFlagsCullBackFacingTrianglesEXT, // keep consistent with your scene (adjust if needed)
        0xFF,
        1, 1, 1,          // missIndex=1 (shadow miss), sbtRecordOffset=1 (shadow hit group)
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
    if (!ENABLE_SHADOWS)
        return 1.0;

    float eps = max(SHADOW_BIAS_MIN, SHADOW_BIAS_SLOPE * gl_HitTEXT);
    vec3  org = P + N * eps;

    if (!ENABLE_DIR_SOFT || angRad <= 0.0 || DIR_SHADOW_SAMPLES <= 1)
        return traceShadow(org, normalize(Ldir), 1e30);

    float sum = 0.0;
    for (int s = 0; s < DIR_SHADOW_SAMPLES; ++s)
    {
        uvec3 key3 = uvec3(gl_LaunchIDEXT.xy, uint(s));
        float r0 = hash13(key3);
        float r1 = hash13(key3 ^ uvec3(12345u, 67890u, 424242u));

        vec3 d = coneSample(Ldir, angRad, vec2(r0, r1));
        sum += traceShadow(org, d, 1e30);
    }

    return sum / float(DIR_SHADOW_SAMPLES);
}

float shadowPointOrSpot(vec3 P, vec3 N, vec3 Ldir, float distToLight)
{
    if (!ENABLE_SHADOWS)
        return 1.0;

    float eps = max(SHADOW_BIAS_MIN, SHADOW_BIAS_SLOPE * gl_HitTEXT);
    vec3  org = P + N * eps;

    // IMPORTANT: tMax should stop at the light (minus a tiny epsilon)
    float tMax = max(distToLight - 0.01, 0.01);
    return traceShadow(org, normalize(Ldir), tMax);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
void main()
{
    const uint instId = gl_InstanceCustomIndexEXT;

    if (instId >= uint(instBuf.insts.length()))
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0);
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
        payload = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    const uint primId = gl_PrimitiveID;
    if (primId >= instDat.triCnt)
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0);
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
    if (dot(nObj, nObj) < 1e-20) nObj = geoNObj;

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

    GpuMat mat = (matCnt > 0u) ? matSsb.mats[matId] : GpuMat(
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

    uint lightCount = min(uLights.info.x, 64u);

    for (uint i = 0u; i < lightCount; ++i)
    {
        GpuLight Ld = uLights.lights[i];

        vec3  L;
        float atten;
        evalLight(Ld, posW, L, atten);

        // Shadows
        if (ENABLE_SHADOWS)
        {
            uint lt = uint(Ld.pos_type.w + 0.5);

            if (lt == GPU_LIGHT_DIRECTIONAL)
            {
                float angRad = max(Ld.dir_range.w, 0.0); // directional softness (radians)
                float vis    = shadowDirectional(posW, N, L, angRad);
                atten *= vis;
            }
            else
            {
                float distToLight = length(Ld.pos_type.xyz - posW);
                float vis         = shadowPointOrSpot(posW, N, L, distToLight);
                atten *= vis;
            }
        }

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

        // Match your raster scale
        const float LIGHT_INTENSITY_SCALE = 5.0;

        vec3 radiance = Ld.color_intensity.rgb *
                        (Ld.color_intensity.a * LIGHT_INTENSITY_SCALE * atten);

        direct += (diff + spec) * radiance * NdotL;
    }

    // --------------------------------------------------------
    // Indirect (studio IBL) + ambient fill (match raster)
    // --------------------------------------------------------
    float hasSceneLights = (uLights.info.x > 1u) ? 1.0 : 0.0;
    float iblScale = mix(1.0, 0.20, hasSceneLights);

    vec3  Fv   = fresnelSchlick(saturate(dot(N, V)), F0);
    vec3  kdI  = (vec3(1.0) - Fv) * (1.0 - metallic);

    vec3 diffIBL = kdI * albedo * studioEnv(N) * 0.10 * ao * iblScale;

    vec3 R = reflect(-V, N);
    vec3 specIBL = studioEnv(R) * Fv * (0.05 + 0.25 * (1.0 - roughness)) * iblScale;

    vec3 floorFill = albedo * 0.004;

    vec3 emissive = mat.emisCol * mat.emisIt;
    if (mat.emisTex >= 0)
        emissive *= texture(tex2d[mat.emisTex], vUv).rgb;

    vec3 ambientFill = vec3(0.0);
    if (USE_AMBIENT_FILL)
        ambientFill = uLights.ambient.rgb * albedo * ao;

    vec3 colorLinear = direct + diffIBL + specIBL + floorFill + emissive + ambientFill;
    colorLinear = max(colorLinear, vec3(0.0));

    // --------------------------------------------------------
    // Exposure + tonemap (MUST match raster)
    // --------------------------------------------------------
    float exposure = FIXED_EXPOSURE;

    if (USE_UBO_EXPOSURE)
    {
        float t = saturate(uLights.ambient.a); // UI 0..1
        float exposureEV = mix(EXPOSURE_EV_MIN, EXPOSURE_EV_MAX, t);
        exposure = exp2(exposureEV);
    }

    vec3 outRgb = colorLinear * exposure;

    if (ENABLE_TONEMAP)
        outRgb = tonemapACES(outRgb);
    else
        outRgb = clamp(outRgb, vec3(0.0), vec3(1.0));

    payload = vec4(outRgb, 1.0);
}
