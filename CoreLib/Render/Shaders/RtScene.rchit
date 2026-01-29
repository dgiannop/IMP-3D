//==============================================================
// RtScene.rchit
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) rayPayloadInEXT vec4 payload;

// IMPORTANT:
// - payloadInEXT is for the incoming primary ray.
// - rayPayloadEXT is for rays we trace FROM this shader (shadow rays).
layout(location = 1) rayPayloadEXT uint shadowHit;

hitAttributeEXT vec2 attribs;

// ------------------------------------------------------------
// Buffer references (device address)
// ------------------------------------------------------------
layout(buffer_reference, scalar) readonly buffer PosBuf   { vec4 p[]; };
layout(buffer_reference, scalar) readonly buffer IdxBuf   { uint idx[]; }; // triCount*4 (padded)
layout(buffer_reference, scalar) readonly buffer NrmBuf   { vec4 n[]; };   // triCount*3
layout(buffer_reference, scalar) readonly buffer UvBuf    { vec4 uv[]; };  // triCount*3
layout(buffer_reference, scalar) readonly buffer MatIdBuf { uint m[]; };   // triCount

// ------------------------------------------------------------
// TLAS (MATCH rgen: set=2, binding=2)
// ------------------------------------------------------------
layout(set = 2, binding = 2) uniform accelerationStructureEXT u_tlas;

// ------------------------------------------------------------
// Per-instance data (MUST match CPU RtInstanceData exactly)
// ------------------------------------------------------------
struct RtInstanceData
{
    uint64_t posAdr;
    uint64_t idxAdr;
    uint64_t nrmAdr;
    uint64_t uvAdr;
    uint64_t matIdAdr;
    uint     triCount;
    uint     _pad0;
    uint     _pad1;
    uint     _pad2;
};

layout(set = 2, binding = 3, std430) readonly buffer Instances
{
    RtInstanceData inst[];
} u_inst;

// ------------------------------------------------------------
// Camera (frame set: set=0, binding=2)
// ------------------------------------------------------------
layout(set = 0, binding = 2, std140) uniform RtCameraUBO
{
    mat4 invViewProj;
    mat4 view;        // WORLD->VIEW
    mat4 invView;     // VIEW->WORLD
    vec4 camPos;      // world
    vec4 clearColor;
} u_cam;

// ------------------------------------------------------------
// Lights (frame set: set=0, binding=1)
// ------------------------------------------------------------
struct GpuLight
{
    vec4 pos_type;
    vec4 dir_range;
    vec4 color_intensity;
    vec4 spot_params; // x=innerCos, y=outerCos
};

layout(set = 0, binding = 1, std140) uniform LightsUBO
{
    uvec4   info;    // x = lightCount
    vec4    ambient; // rgb, a
    GpuLight lights[64];
} uLights;

// ------------------------------------------------------------
// Materials + textures (MATCH raster bindings)
// ------------------------------------------------------------
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

// ------------------------------------------------------------
// Shadow trace (Directional only, hard shadows)
//
// Pipeline layout:
//   Group 0: raygen
//   Group 1: primary miss
//   Group 2: primary hit
//   Group 3: shadow miss
//   Group 4: shadow hit
//
// SBT layout in RtSbt:
//   Miss[0] = group 1 (primary miss)
//   Miss[1] = group 3 (shadow miss)
//   Hit[0]  = group 2 (primary hit)
//   Hit[1]  = group 4 (shadow hit)
//
// So for shadow rays:
//   sbtRecordOffset = 1 (Hit[1])
//   sbtRecordStride = 1
//   missIndex       = 1 (Miss[1])
// ------------------------------------------------------------
float trace_shadow_dir(vec3 Pw, vec3 Nw, vec3 Lw_dir)
{
    const float kEps = 1e-3;

    vec3 org = Pw + Nw * kEps;
    vec3 dir = normalize(Lw_dir);

    shadowHit = 0u;

    traceRayEXT(
        u_tlas,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
        0xFF,
        1, // sbtRecordOffset: Hit[1] = shadow hit
        1, // sbtRecordStride
        1, // missIndex: Miss[1] = shadow miss
        org,
        0.001,
        dir,
        1e30,
        1  // payload location = 1
    );

    return (shadowHit == 0u) ? 1.0 : 0.0;
}

void main()
{
    const uint instId = gl_InstanceCustomIndexEXT;

    if (instId >= u_inst.inst.length())
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    RtInstanceData d = u_inst.inst[instId];

    if (d.posAdr == 0ul || d.idxAdr == 0ul || d.nrmAdr == 0ul || d.uvAdr == 0ul || d.matIdAdr == 0ul || d.triCount == 0u)
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    const uint prim = gl_PrimitiveID;
    if (prim >= d.triCount)
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    PosBuf   pos = PosBuf(d.posAdr);
    IdxBuf   ind = IdxBuf(d.idxAdr);
    NrmBuf   nrm = NrmBuf(d.nrmAdr);
    UvBuf    uvb = UvBuf(d.uvAdr);
    MatIdBuf mid = MatIdBuf(d.matIdAdr);

    // World-space hit point
    vec3 Pw = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // Triangle indices
    const uint base4 = prim * 4u;
    const uint ia    = ind.idx[base4 + 0u];
    const uint ib    = ind.idx[base4 + 1u];
    const uint ic    = ind.idx[base4 + 2u];

    vec3 a = pos.p[ia].xyz;
    vec3 b = pos.p[ib].xyz;
    vec3 c = pos.p[ic].xyz;

    // Barycentrics
    const float b1 = attribs.x;
    const float b2 = attribs.y;
    const float b0 = 1.0 - b1 - b2;

    // Per-corner normals/uvs
    const uint base3 = prim * 3u;

    vec3 n0 = nrm.n[base3 + 0u].xyz;
    vec3 n1 = nrm.n[base3 + 1u].xyz;
    vec3 n2 = nrm.n[base3 + 2u].xyz;

    vec3 Nw = normalize(n0 * b0 + n1 * b1 + n2 * b2);
    if (dot(Nw, Nw) < 1e-20)
        Nw = normalize(cross(b - a, c - a));

    vec2 uv0 = uvb.uv[base3 + 0u].xy;
    vec2 uv1 = uvb.uv[base3 + 1u].xy;
    vec2 uv2 = uvb.uv[base3 + 2u].xy;
    vec2 UV  = uv0 * b0 + uv1 * b1 + uv2 * b2;

    // View-space for shading
    vec3 Pv = (u_cam.view * vec4(Pw, 1.0)).xyz;
    vec3 Nv = normalize(mat3(u_cam.view) * Nw);
    vec3 V  = normalize(-Pv);

    // Material fetch
    const uint matIdRaw = mid.m[prim];
    const uint matCount = materials.length();
    const uint matId    = (matCount > 0u) ? min(matIdRaw, matCount - 1u) : 0u;

    GpuMaterial mat = (matCount > 0u) ? materials[matId] : GpuMaterial(
        vec3(1,0,1), 1.0,
        vec3(0), 0.0,
        0.5, 0.0, 1.5, 0.0,
        -1, -1, -1, -1);

    // Albedo
    vec3 albedo = mat.baseColor;
    if (mat.baseColorTexture >= 0)
        albedo *= texture(uTextures[mat.baseColorTexture], UV).rgb;

    // Params (+ MRAO)
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float metallic  = clamp(mat.metallic,  0.0,  1.0);
    float ao        = 1.0;

    if (mat.mraoTexture >= 0)
    {
        vec3 mrao = texture(uTextures[mat.mraoTexture], UV).rgb;
        ao        = clamp(mrao.r, 0.0, 1.0);
        roughness = clamp(roughness * mrao.g, 0.04, 1.0);
        metallic  = clamp(metallic  * mrao.b, 0.0,  1.0);
    }

    float alpha = roughness * roughness;

    float ior = max(mat.ior, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08));
    vec3  F0  = mix(F0d, albedo, metallic);

    // Ambient
    float ambStrength = (uLights.ambient.a > 0.0) ? uLights.ambient.a : 0.05;
    vec3  ambient     = uLights.ambient.rgb * ambStrength * albedo * ao;

    // Direct lighting
    vec3 direct = vec3(0.0);

    const uint lightCount = uLights.info.x;
    if (lightCount == 0u)
    {
        payload = vec4(ambient + albedo * 0.02, 1.0);
        return;
    }

    for (uint i = 0u; i < lightCount; ++i)
    {
        uint t = uint(uLights.lights[i].pos_type.w + 0.5);

        vec3  L = vec3(0.0);
        float atten = 1.0;

        if (t == 0u) // Directional (HEADLIGHT in view space)
        {
            // Convention:
            //  - dir_range.xyz = light "forward" direction in VIEW space
            //  - surface->light = -forward (same as raster path)
            vec3 L_view  = normalize(-uLights.lights[i].dir_range.xyz);

            // Convert surface->light to WORLD for shadow rays
            vec3 L_world = normalize((u_cam.invView * vec4(L_view, 0.0)).xyz);

            float visibility = trace_shadow_dir(Pw, Nw, L_world);

            L      = L_view;      // BRDF uses VIEW space
            atten *= visibility;  // [0..1]
        }
        else if (t == 1u) // Point (no shadows yet)
        {
            vec3 toLight = uLights.lights[i].pos_type.xyz - Pv;
            float d2 = max(dot(toLight, toLight), 1e-8);
            float d  = sqrt(d2);
            L = toLight / d;

            float range = uLights.lights[i].dir_range.w;
            if (range > 0.0)
            {
                float x = saturate(1.0 - (d / range));
                atten = x * x;
            }
            else
            {
                atten = 1.0 / d2;
            }
        }
        else if (t == 2u) // Spot (no shadows yet)
        {
            vec3 toLight = uLights.lights[i].pos_type.xyz - Pv;
            float d2 = max(dot(toLight, toLight), 1e-8);
            float d  = sqrt(d2);
            L = toLight / d;

            float range = uLights.lights[i].dir_range.w;
            if (range > 0.0)
            {
                float x = saturate(1.0 - (d / range));
                atten = x * x;
            }
            else
            {
                atten = 1.0 / d2;
            }

            vec3  spotFwd = normalize(uLights.lights[i].dir_range.xyz);
            float cosAng  = dot(-L, spotFwd);

            float innerCos = uLights.lights[i].spot_params.x;
            float outerCos = uLights.lights[i].spot_params.y;

            float cone = saturate((cosAng - outerCos) / max(innerCos - outerCos, 1e-5));
            atten *= cone * cone;
        }
        else
        {
            continue;
        }

        float NdotL = saturate(dot(Nv, L));
        float NdotV = saturate(dot(Nv, V));
        if (NdotL <= 0.0 || NdotV <= 0.0)
            continue;

        vec3 H = normalize(V + L);

        float NdotH = saturate(dot(Nv, H));
        float VdotH = saturate(dot(V, H));

        vec3  F = fresnelSchlick(VdotH, F0);
        float D = D_GGX(NdotH, alpha);

        float r = roughness + 1.0;
        float k = (r * r) / 8.0;
        float G = G_Smith(NdotV, NdotL, k);

        vec3  specBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-7);
        vec3  kd       = (vec3(1.0) - F) * (1.0 - metallic);
        vec3  diffBRDF = kd * albedo / 3.14159265;

        vec3  c = uLights.lights[i].color_intensity.rgb;
        float I = uLights.lights[i].color_intensity.a;

        vec3 radiance = c * I;

        direct += (diffBRDF + specBRDF) * radiance * (NdotL * atten);
    }

    // Emissive
    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
    if (mat.emissiveTexture >= 0)
        emissive *= texture(uTextures[mat.emissiveTexture], UV).rgb;

    vec3 color = ambient + direct + emissive;
    color = max(color, vec3(0.0));

    payload = vec4(color, 1.0);
}
