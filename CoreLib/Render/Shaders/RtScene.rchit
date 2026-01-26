// ============================================================
// RtScene.rchit
// ============================================================
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;

// ------------------------------------------------------------
// Buffer references (device address)
// ------------------------------------------------------------
layout(buffer_reference, scalar) readonly buffer PosBuf { vec4 p[]; };
layout(buffer_reference, scalar) readonly buffer IdxBuf { uint idx[]; }; // padded uvec4 per tri
layout(buffer_reference, scalar) readonly buffer NrmBuf { vec4 n[]; };   // triCount*3
layout(buffer_reference, scalar) readonly buffer UvBuf  { vec4 uv[]; };  // triCount*3
layout(buffer_reference, scalar) readonly buffer MatIdBuf { uint m[]; }; // triCount

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

layout(set = 0, binding = 4, std430) readonly buffer Instances
{
    RtInstanceData inst[];
} u_inst;

// ------------------------------------------------------------
// Camera (now includes view matrix)
// ------------------------------------------------------------
layout(set = 0, binding = 2, std140) uniform RtCameraUBO
{
    mat4 invViewProj;
    mat4 view;        // NEW
    vec4 camPos;      // world
    vec4 clearColor;
} u_cam;

// ------------------------------------------------------------
// Lights (same semantics as raster: VIEW-SPACE)
// ------------------------------------------------------------
struct GpuLight
{
    vec4 pos_type;        // w = type
    vec4 dir_range;       // xyz = (surface -> light) in VIEW SPACE
    vec4 color_intensity; // rgb = color, a = intensity
    vec4 spot_params;
};

layout(set = 0, binding = 5, std140) uniform LightsUBO
{
    uvec4   info;         // x = lightCount
    vec4    ambient;      // rgb, a
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
// Helpers (GGX / Cook-Torrance) — identical to raster
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
    float ggxV = G_SchlickGGX(NdotV, k);
    float ggxL = G_SchlickGGX(NdotL, k);
    return ggxV * ggxL;
}

void main()
{
    const uint instId = gl_InstanceCustomIndexEXT;
    RtInstanceData d  = u_inst.inst[instId];

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

    // -----------------------------
    // World-space hit point
    // -----------------------------
    vec3 P = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // -----------------------------
    // Triangle data
    // -----------------------------
    const uint base4 = prim * 4u;
    const uint ia = ind.idx[base4 + 0u];
    const uint ib = ind.idx[base4 + 1u];
    const uint ic = ind.idx[base4 + 2u];

    vec3 a = pos.p[ia].xyz;
    vec3 b = pos.p[ib].xyz;
    vec3 c = pos.p[ic].xyz;

    // Barycentrics
    const float b1 = attribs.x;
    const float b2 = attribs.y;
    const float b0 = 1.0 - b1 - b2;

    // Per-corner normals/uvs (assumed WORLD SPACE, as uploaded)
    const uint base3 = prim * 3u;

    vec3 n0 = nrm.n[base3 + 0u].xyz;
    vec3 n1 = nrm.n[base3 + 1u].xyz;
    vec3 n2 = nrm.n[base3 + 2u].xyz;

    vec3 Nw = normalize(n0 * b0 + n1 * b1 + n2 * b2);
    if (any(isnan(Nw)) || dot(Nw, Nw) < 1e-20)
        Nw = normalize(cross(b - a, c - a));

    vec2 uv0 = uvb.uv[base3 + 0u].xy;
    vec2 uv1 = uvb.uv[base3 + 1u].xy;
    vec2 uv2 = uvb.uv[base3 + 2u].xy;
    vec2 UV  = uv0 * b0 + uv1 * b1 + uv2 * b2;

    // -----------------------------
    // Convert to VIEW SPACE (to match raster headlight lighting)
    // -----------------------------
    vec3 Pv = (u_cam.view * vec4(P, 1.0)).xyz;          // view-space position
    vec3 Nv = normalize(mat3(u_cam.view) * Nw);         // view-space normal
    vec3 V  = normalize(-Pv);                           // view-space view vector (camera at origin)

    // -----------------------------
    // Material fetch
    // -----------------------------
    const uint matIdRaw = mid.m[prim];
    const uint matCount = materials.length();
    const uint matId    = (matCount > 0u) ? min(matIdRaw, matCount - 1u) : 0u;

    GpuMaterial mat = (matCount > 0u) ? materials[matId] : GpuMaterial(
        vec3(1,0,1), 1.0,
        vec3(0), 0.0,
        0.5, 0.0, 1.5, 0.0,
        -1, -1, -1, -1);

    // -----------------------------
    // Base color (albedo)
    // -----------------------------
    vec3 albedo = mat.baseColor;
    if (mat.baseColorTexture >= 0)
        albedo *= texture(uTextures[mat.baseColorTexture], UV).rgb;

    // -----------------------------
    // Material params (+ optional MRAO like raster)
    // -----------------------------
    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float metallic  = clamp(mat.metallic,  0.0,  1.0);
    float ao        = 1.0;

    if (mat.mraoTexture >= 0)
    {
        vec3 mrao = texture(uTextures[mat.mraoTexture], UV).rgb; // linear
        ao        = clamp(mrao.r, 0.0, 1.0);
        roughness = clamp(roughness * mrao.g, 0.04, 1.0);
        metallic  = clamp(metallic  * mrao.b, 0.0,  1.0);
    }

    float alpha = roughness * roughness;

    float ior = max(mat.ior, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08));
    vec3  F0  = mix(F0d, albedo, metallic);

    // -----------------------------
    // Direct lighting from GpuLightsUBO (VIEW SPACE)
    // -----------------------------
    vec3 direct = vec3(0.0);

    const uint lightCount = uLights.info.x;
    for (uint i = 0u; i < lightCount; ++i)
    {
        vec3 L = normalize(uLights.lights[i].dir_range.xyz); // surface -> light (view space)
        vec3 H = normalize(V + L);

        float NdotL = saturate(dot(Nv, L));
        float NdotV = saturate(dot(Nv, V));
        float NdotH = saturate(dot(Nv, H));
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0 && NdotV > 0.0)
        {
            vec3  F = fresnelSchlick(VdotH, F0);
            float D = D_GGX(NdotH, alpha);

            float r = roughness + 1.0;
            float k = (r * r) / 8.0;
            float G = G_Smith(NdotV, NdotL, k);

            vec3  specularBRDF = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-7);
            vec3  kd           = (vec3(1.0) - F) * (1.0 - metallic);
            vec3  diffuseBRDF  = kd * albedo / 3.14159265;

            vec3 radiance = uLights.lights[i].color_intensity.rgb *
                            uLights.lights[i].color_intensity.a;

            direct += (diffuseBRDF + specularBRDF) * radiance * NdotL;
        }
    }

    // Ambient from UBO (match raster behavior)
    vec3 ambient = uLights.ambient.rgb * uLights.ambient.a * albedo;

    // Emissive
    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
    if (mat.emissiveTexture >= 0)
        emissive *= texture(uTextures[mat.emissiveTexture], UV).rgb;

    // AO affects indirect-ish parts (keep it simple here)
    ambient *= ao;

    vec3 color = ambient + direct + emissive;
    payload = vec4(color, 1.0);
}
