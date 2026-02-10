//==============================================================
// RtScene.rchit  (Primary shading + optional headlight shadows)
//==============================================================
// Conventions expected by this shader:
// - camUbo.viewM  = WORLD -> VIEW
// - camUbo.invV   = VIEW  -> WORLD
// - Directional light encoding matches raster:
//   * dirRng.xyz = forward direction the light points TOWARD (VIEW space)
//   * surface->light (VIEW) = -forward
//   * dirRng.w   = angular radius (radians) for soft shadows (optional)
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
// Camera UBO (frame set)
// ------------------------------------------------------------
layout(set = 0, binding = 2, std140) uniform CamUBO
{
    mat4 invVP;
    mat4 viewM;      // WORLD -> VIEW
    mat4 invV;       // VIEW  -> WORLD
    vec4 camPos;     // world
    vec4 clrCol;
} camUbo;

// ------------------------------------------------------------
// Lights UBO (frame set)
// ------------------------------------------------------------
struct GpuLit
{
    vec4 posTyp;     // xyz = pos (view), w = type
    vec4 dirRng;     // xyz = forward dir (view), w = range (pt/spot) OR angular radius (dir)
    vec4 colInt;     // rgb = color, a = intensity
    vec4 spotPr;     // x = innerCos, y = outerCos
};

layout(set = 0, binding = 1, std140) uniform LitUBO
{
    uvec4 info4;     // x = lightCount
    vec4  ambExp;    // rgb = ambient fill, a = exposure (optional)
    GpuLit lits[64];
} litUbo;

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
// Soft shadow helpers
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

float shadDir(vec3 hitPos, vec3 hitNrm, vec3 dirWld, float angRad)
{
    const int   sampCt = 4;
    const float epsVal = 1e-3;

    vec3 rayOrg = hitPos + hitNrm * epsVal;
    float visSum = 0.0;

    for (int sampIx = 0; sampIx < sampCt; ++sampIx)
    {
        uvec3 key3 = uvec3(gl_LaunchIDEXT.xy, uint(sampIx));
        float rndA = hash13(key3);
        float rndB = hash13(key3 ^ uvec3(12345u, 67890u, 424242u));

        vec3 rayDir = (angRad > 0.0)
                    ? coneSmpl(dirWld, angRad, vec2(rndA, rndB))
                    : normalize(dirWld);

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

    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

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

    vec2 uvvA = uvvBuf.uvv4[base3 + 0u].xy;
    vec2 uvvB = uvvBuf.uvv4[base3 + 1u].xy;
    vec2 uvvC = uvvBuf.uvv4[base3 + 2u].xy;
    vec2 texUV = uvvA * barA + uvvB * barB + uvvC * barC;

    vec3 posViw = (camUbo.viewM * vec4(hitPos, 1.0)).xyz;
    vec3 nrmViw = normalize(mat3(camUbo.viewM) * nrmWld);
    vec3 dirViw = normalize(-posViw);

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

    float ambStr = (litUbo.ambExp.a > 0.0) ? litUbo.ambExp.a : 0.05;
    vec3  ambCol = litUbo.ambExp.rgb * ambStr * albCol * aoVal;

    vec3 lgtSum = vec3(0.0);

    const uint litCnt = litUbo.info4.x;
    if (litCnt == 0u)
    {
        colorOut = vec4(ambCol + albCol * 0.02, 1.0);
        return;
    }

    for (uint litIdx = 0u; litIdx < litCnt; ++litIdx)
    {
        uint litTyp = uint(litUbo.lits[litIdx].posTyp.w + 0.5);

        vec3  litDir = vec3(0.0); // surface->light in VIEW
        float attVal = 1.0;

        if (litTyp == 0u) // Directional
        {
            vec3 fwdViw = normalize(litUbo.lits[litIdx].dirRng.xyz);
            vec3 dirToL = normalize(-fwdViw); // surface->light in VIEW
            float angRad = max(litUbo.lits[litIdx].dirRng.w, 0.0);

            vec3 dirWld = normalize((camUbo.invV * vec4(dirToL, 0.0)).xyz);
            float visVal = shadDir(hitPos, nrmWld, dirWld, angRad);

            litDir = dirToL;
            attVal *= visVal;
        }
        else if (litTyp == 1u) // Point
        {
            vec3 toLgt = litUbo.lits[litIdx].posTyp.xyz - posViw;
            float dst2 = max(dot(toLgt, toLgt), 1e-8);
            float dstV = sqrt(dst2);
            litDir     = toLgt / dstV;

            float rngV = litUbo.lits[litIdx].dirRng.w;
            if (rngV > 0.0)
            {
                float facV = satVal(1.0 - (dstV / rngV));
                attVal = facV * facV;
            }
            else
            {
                attVal = 1.0 / dst2;
            }
        }
        else if (litTyp == 2u) // Spot
        {
            vec3 toLgt = litUbo.lits[litIdx].posTyp.xyz - posViw;
            float dst2 = max(dot(toLgt, toLgt), 1e-8);
            float dstV = sqrt(dst2);
            litDir     = toLgt / dstV;

            float rngV = litUbo.lits[litIdx].dirRng.w;
            if (rngV > 0.0)
            {
                float facV = satVal(1.0 - (dstV / rngV));
                attVal = facV * facV;
            }
            else
            {
                attVal = 1.0 / dst2;
            }

            vec3  spotF = normalize(litUbo.lits[litIdx].dirRng.xyz);
            float cosAn = dot(-litDir, spotF);

            float innCs = litUbo.lits[litIdx].spotPr.x;
            float outCs = litUbo.lits[litIdx].spotPr.y;

            float coneV = satVal((cosAn - outCs) / max(innCs - outCs, 1e-5));
            attVal *= coneV * coneV;
        }
        else
        {
            continue;
        }

        float ndlVal = satVal(dot(nrmViw, litDir));
        float ndvVal = satVal(dot(nrmViw, dirViw));
        if (ndlVal <= 0.0 || ndvVal <= 0.0)
            continue;

        vec3 halfV = normalize(dirViw + litDir);

        float ndhVal = satVal(dot(nrmViw, halfV));
        float vdhVal = satVal(dot(dirViw, halfV));

        vec3  frsCol = fresShk(vdhVal, f0Col);
        float dVal   = dGgx(ndhVal, alpVal);

        float rou1 = rouVal + 1.0;
        float kVal = (rou1 * rou1) / 8.0;
        float gVal = gSmt(ndvVal, ndlVal, kVal);

        vec3 specBr = (dVal * gVal * frsCol) / max(4.0 * ndvVal * ndlVal, 1e-7);
        vec3 kdVal  = (vec3(1.0) - frsCol) * (1.0 - metVal);
        vec3 difBr  = kdVal * albCol / 3.14159265;

        vec3  litCol = litUbo.lits[litIdx].colInt.rgb;
        float litInt = litUbo.lits[litIdx].colInt.a;

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
