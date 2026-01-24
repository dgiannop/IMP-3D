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
    // World-space hit point + view
    // -----------------------------
    vec3 P = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 V = normalize(gl_WorldRayOriginEXT - P);

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

    // Per-corner normals/uvs
    const uint base3 = prim * 3u;

    vec3 n0 = nrm.n[base3 + 0u].xyz;
    vec3 n1 = nrm.n[base3 + 1u].xyz;
    vec3 n2 = nrm.n[base3 + 2u].xyz;

    vec3 N = normalize(n0 * b0 + n1 * b1 + n2 * b2);
    if (any(isnan(N)) || dot(N, N) < 1e-20)
        N = normalize(cross(b - a, c - a));

    vec2 uv0 = uvb.uv[base3 + 0u].xy;
    vec2 uv1 = uvb.uv[base3 + 1u].xy;
    vec2 uv2 = uvb.uv[base3 + 2u].xy;
    vec2 UV  = uv0 * b0 + uv1 * b1 + uv2 * b2;

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
    // Base color (albedo) + texture
    // -----------------------------
    vec3 albedo = mat.baseColor;

    if (mat.baseColorTexture >= 0)
    {
        // Same sampling as raster:
        albedo *= texture(uTextures[mat.baseColorTexture], UV).rgb;
    }

    // -----------------------------
    // Light (world space)
    // -----------------------------
    vec3 L = normalize(vec3(0.3, 0.7, 0.2)); // pick a world-space light dir
    vec3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    if (NdotL <= 0.0 || NdotV <= 0.0)
    {
        vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;
        vec3 ambient  = albedo * 0.20;
        payload = vec4(ambient + emissive, 1.0);
        return;
    }

    float roughness = clamp(mat.roughness, 0.04, 1.0);
    float metallic  = clamp(mat.metallic,  0.0,  1.0);
    float alpha     = roughness * roughness;

    float ior = max(mat.ior, 1.0);
    float f0s = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3  F0d = vec3(clamp(f0s, 0.02, 0.08));

    vec3 F0 = mix(F0d, albedo, metallic);

    vec3  F = fresnelSchlick(VdotH, F0);
    float D = D_GGX(NdotH, alpha);

    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float G = G_Smith(NdotV, NdotL, k);

    vec3  numerator    = D * G * F;
    float denom        = max(4.0 * NdotV * NdotL, 1e-7);
    vec3  specularBRDF = numerator / denom;

    vec3 kd          = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuseBRDF = kd * albedo / 3.14159265;

    vec3 radiance = vec3(1.0);

    vec3 direct   = (diffuseBRDF + specularBRDF) * radiance * NdotL;
    vec3 ambient  = albedo * 0.50;
    vec3 emissive = mat.emissiveColor * mat.emissiveIntensity;

    vec3 color = ambient + direct + emissive;

    payload = vec4(color, 1.0);
}
