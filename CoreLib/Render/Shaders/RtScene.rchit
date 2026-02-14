//==============================================================
// RtScene.rchit  (Primary shading + optional headlight shadows)
//==============================================================
// WORLD-space lighting conventions (post-refactor):
// - Directional:
//   * lights[i].dir_range.xyz = light forward direction in WORLD space
//   * surface->light (WORLD)  = -forward
//   * lights[i].dir_range.w   = angular radius (radians) for soft shadows (optional)
// - Point/Spot:
//   * lights[i].pos_type.xyz  = position in WORLD space
//   * lights[i].dir_range.xyz = forward direction in WORLD space (spot only)
//   * lights[i].dir_range.w   = range
//==============================================================

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) rayPayloadInEXT vec4 colorOut;
layout(location = 1) rayPayloadEXT uint occFlag;

hitAttributeEXT vec2 hitAttr;

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
// Camera UBO (frame set, unified with raster/RT)
// set = 0, binding = 0
// ------------------------------------------------------------
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    mat4 viewProj;

    mat4 invProj;
    mat4 invView;
    mat4 invViewProj;

    vec4 camPos;     // world
    vec4 viewport;   // (w, h, 1/w, 1/h)
    vec4 clearColor; // RT clear color
} uCamera;

// ------------------------------------------------------------
// Lights UBO (WORLD space; shared with raster)
// set = 0, binding = 1
// ------------------------------------------------------------
struct GpuLight
{
    vec4 pos_type;        // xyz = pos (WORLD) for point/spot, w = type
    vec4 dir_range;       // xyz = forward dir (WORLD), w = range OR angular radius
    vec4 color_intensity; // rgb = color, a = intensity
    vec4 spot_params;     // x = innerCos, y = outerCos
};

layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uvec4    info;    // x = lightCount
    vec4     ambient; // rgb = ambient fill, a = exposure (optional)
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
float satVal(float val) { return clamp(val, 0.0, 1.0); }

vec3 fresShk(float cosVh, vec3 f0Col)
{
    float oneMh = 1.0 - satVal(cosVh);
    float pow5  = oneMh * oneMh;
    pow5        = pow5 * pow5 * oneMh;
    return f0Col + (vec3(1.0) - f0Col) * pow5;
}

float dGgx(float ndhVal, float alpVal)
{
    float a2Val = alpVal * alpVal;
    float denV  = (ndhVal * ndhVal) * (a2Val - 1.0) + 1.0;
    return a2Val / max(3.14159265 * denV * denV, 1e-7);
}

float gSch(float ndxVal, float kVal)
{
    return ndxVal / max(ndxVal * (1.0 - kVal) + kVal, 1e-7);
}

float gSmt(float ndvVal, float ndlVal, float kVal)
{
    return gSch(ndvVal, kVal) * gSch(ndlVal, kVal);
}

// ------------------------------------------------------------
// Soft shadow helpers (WORLD)
// ------------------------------------------------------------
float hash13(uvec3 key3)
{
    key3 = (key3 ^ (key3 >> 16u)) * 0x7feb352du;
    key3 = (key3 ^ (key3 >> 15u)) * 0x846ca68bu;
    key3 = (key3 ^ (key3 >> 16u));
    uint mixV = key3.x ^ key3.y ^ key3.z;
    return float(mixV) / 4294967295.0;
}

vec3 coneSmpl(vec3 dirIn, float angRad, vec2 rnd2)
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

float shadDir(vec3 hitPosW, vec3 hitNrmW, vec3 dirToLightW, float angRad)
{
    const int   sampCt = 4;
    float epsVal = max(1e-3, 1e-4 * gl_HitTEXT);

    vec3  rayOrg = hitPosW + hitNrmW * epsVal;
    float visSum = 0.0;

    for (int sampIx = 0; sampIx < sampCt; ++sampIx)
    {
        uvec3 key3 = uvec3(gl_LaunchIDEXT.xy, uint(sampIx));
        float rndA = hash13(key3);
        float rndB = hash13(key3 ^ uvec3(12345u, 67890u, 424242u));

        vec3 rayDir = (angRad > 0.0)
                    ? coneSmpl(dirToLightW, angRad, vec2(rndA, rndB))
                    : normalize(dirToLightW);

        occFlag = 0u;

        traceRayEXT(
            tlasTop,
            gl_RayFlagsTerminateOnFirstHitEXT |
            gl_RayFlagsOpaqueEXT |
            gl_RayFlagsCullBackFacingTrianglesEXT,
            0xFF,
            1, // Hit[1]
            1, // stride
            1, // Miss[1]
            rayOrg,
            0.001,
            rayDir,
            1e30,
            1
        );

        visSum += (occFlag == 0u) ? 1.0 : 0.0;
    }

    return visSum / float(sampCt);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
void main()
{
    const uint instId = gl_InstanceCustomIndexEXT;

    if (instId >= instBuf.insts.length())
    {
        colorOut = vec4(1.0, 0.0, 1.0, 1.0);
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
        colorOut = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    const uint primId = gl_PrimitiveID;
    if (primId >= instDat.triCnt)
    {
        colorOut = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    PosBuf posBuf = PosBuf(instDat.posAdr);
    IdxBuf idxBuf = IdxBuf(instDat.idxAdr);
    NrmBuf nrmBuf = NrmBuf(instDat.nrmAdr);
    UvBuf  uvvBuf = UvBuf(instDat.uvvAdr);
    MatBuf matBuf = MatBuf(instDat.matAdr);

    vec3 hitPosW = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    const uint base4 = primId * 4u;
    const uint idxA  = idxBuf.ind4[base4 + 0u];
    const uint idxB  = idxBuf.ind4[base4 + 1u];
    const uint idxC  = idxBuf.ind4[base4 + 2u];

    vec3 posA = posBuf.pos4[idxA].xyz;
    vec3 posB = posBuf.pos4[idxB].xyz;
    vec3 posC = posBuf.pos4[idxC].xyz;

    const float barB = hitAttr.x;
    const float barC = hitAttr.y;
    const float barA = 1.0 - barB - barC;

    const uint base3 = primId * 3u;

    vec3 nrmA = nrmBuf.nrm4[base3 + 0u].xyz;
    vec3 nrmB = nrmBuf.nrm4[base3 + 1u].xyz;
    vec3 nrmC = nrmBuf.nrm4[base3 + 2u].xyz;

    vec3 nrmWld = normalize(nrmA * barA + nrmB * barB + nrmC * barC);
    if (dot(nrmWld, nrmWld) < 1e-20)
        nrmWld = normalize(cross(posB - posA, posC - posA));

    vec2 uvvA  = uvvBuf.uvv4[base3 + 0u].xy;
    vec2 uvvB  = uvvBuf.uvv4[base3 + 1u].xy;
    vec2 uvvC  = uvvBuf.uvv4[base3 + 2u].xy;
    vec2 texUV = uvvA * barA + uvvB * barB + uvvC * barC;

    // View vector in WORLD space
    vec3 Vw = normalize(uCamera.camPos.xyz - hitPosW);

    const uint matRaw = matBuf.mat1[primId];
    const uint matCnt = matSsb.mats.length();
    const uint matId  = (matCnt > 0u) ? min(matRaw, matCnt - 1u) : 0u;

    GpuMat matDat = (matCnt > 0u) ? matSsb.mats[matId] : GpuMat(
        vec3(1, 0, 1), 1.0,
        vec3(0),       0.0,
        0.5, 0.0, 1.5, 0.0,
        -1, -1, -1, -1);

    vec3 albCol = matDat.baseCol;
    if (matDat.baseTex >= 0)
        albCol *= texture(tex2d[matDat.baseTex], texUV).rgb;

    float rouVal = clamp(matDat.roughIt, 0.04, 1.0);
    float metVal = clamp(matDat.metalIt, 0.0, 1.0);
    float aoVal  = 1.0;

    if (matDat.mraoTex >= 0)
    {
        vec3 mraCol = texture(tex2d[matDat.mraoTex], texUV).rgb;
        aoVal       = clamp(mraCol.r, 0.0, 1.0);
        rouVal      = clamp(rouVal * mraCol.g, 0.04, 1.0);
        metVal      = clamp(metVal * mraCol.b, 0.0, 1.0);
    }

    float alpVal = rouVal * rouVal;

    float iorVal = max(matDat.iorVal, 1.0);
    float f0Sca  = pow((iorVal - 1.0) / (iorVal + 1.0), 2.0);
    vec3  f0Die  = vec3(clamp(f0Sca, 0.02, 0.08));
    vec3  f0Col  = mix(f0Die, albCol, metVal);

    // Ambient + exposure from LightsUBO (same convention you had)
    float ambStr = (uLights.ambient.a > 0.0) ? uLights.ambient.a : 0.05;
    vec3  ambCol = uLights.ambient.rgb * ambStr * albCol * aoVal;

    vec3 lgtSum = vec3(0.0);

    const uint litCnt = min(uLights.info.x, 64u);
    if (litCnt == 0u)
    {
        colorOut = vec4(ambCol + albCol * 0.02, 1.0);
        return;
    }

    for (uint litIdx = 0u; litIdx < litCnt; ++litIdx)
    {
        GpuLight Ld    = uLights.lights[litIdx];
        uint     litTyp = uint(Ld.pos_type.w + 0.5);

        vec3  Lw    = vec3(0.0); // surface->light in WORLD
        float attVal = 1.0;

        if (litTyp == 0u) // Directional
        {
            vec3 fwdW = normalize(Ld.dir_range.xyz);
            Lw        = normalize(-fwdW);

            float angRad = max(Ld.dir_range.w, 0.0);
            float visVal = shadDir(hitPosW, nrmWld, Lw, angRad);
            attVal *= visVal;
        }
        else if (litTyp == 1u) // Point
        {
            vec3  toLgt = Ld.pos_type.xyz - hitPosW;
            float dst2  = max(dot(toLgt, toLgt), 1e-8);
            float dstV  = sqrt(dst2);
            Lw          = toLgt / dstV;

            // Match raster: inverse-square * optional range window
            attVal = 1.0 / dst2;

            float rngV = Ld.dir_range.w;
            if (rngV > 0.0)
            {
                float facV = satVal(1.0 - (dstV / rngV));
                attVal *= facV * facV;
            }
        }
        else if (litTyp == 2u) // Spot
        {
            vec3  toLgt = Ld.pos_type.xyz - hitPosW;
            float dst2  = max(dot(toLgt, toLgt), 1e-8);
            float dstV  = sqrt(dst2);
            Lw          = toLgt / dstV;

            attVal = 1.0 / dst2;

            float rngV = Ld.dir_range.w;
            if (rngV > 0.0)
            {
                float facV = satVal(1.0 - (dstV / rngV));
                attVal *= facV * facV;
            }

            vec3  spotF = normalize(Ld.dir_range.xyz);     // forward (WORLD)
            float cosAn = dot(normalize(-Lw), spotF);       // compare light->surface vs forward

            float innCs = Ld.spot_params.x;
            float outCs = Ld.spot_params.y;

            float coneV = satVal((cosAn - outCs) / max(innCs - outCs, 1e-5));
            attVal *= coneV * coneV;
        }
        else
        {
            continue;
        }

        float ndlVal = satVal(dot(nrmWld, Lw));
        float ndvVal = satVal(dot(nrmWld, Vw));
        if (ndlVal <= 0.0 || ndvVal <= 0.0)
            continue;

        vec3 halfV = normalize(Vw + Lw);

        float ndhVal = satVal(dot(nrmWld, halfV));
        float vdhVal = satVal(dot(Vw, halfV));

        vec3  frsCol = fresShk(vdhVal, f0Col);
        float dVal   = dGgx(ndhVal, alpVal);

        float rou1 = rouVal + 1.0;
        float kVal = (rou1 * rou1) / 8.0;
        float gVal = gSmt(ndvVal, ndlVal, kVal);

        vec3 specBr = (dVal * gVal * frsCol) / max(4.0 * ndvVal * ndlVal, 1e-7);
        vec3 kdVal  = (vec3(1.0) - frsCol) * (1.0 - metVal);
        vec3 difBr  = kdVal * albCol / 3.14159265;

        vec3  litCol = Ld.color_intensity.rgb;
        float litInt = Ld.color_intensity.a;

        vec3 radCol = litCol * litInt;

        lgtSum += (difBr + specBr) * radCol * (ndlVal * attVal);
    }

    vec3 emiCol = matDat.emisCol * matDat.emisIt;
    if (matDat.emisTex >= 0)
        emiCol *= texture(tex2d[matDat.emisTex], texUV).rgb;

    vec3 outCol = ambCol + lgtSum + emiCol;
    outCol = max(outCol, vec3(0.0));

    colorOut = vec4(outCol, 1.0);
}
