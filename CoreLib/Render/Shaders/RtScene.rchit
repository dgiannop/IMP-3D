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

layout(buffer_reference, scalar) readonly buffer PosBuf
{
    vec4 p[]; // xyz position
};

layout(buffer_reference, scalar) readonly buffer IdxBuf
{
    uint idx[]; // PADDED: uvec4 per triangle => [a,b,c,0] => stride 4 uints
};

layout(buffer_reference, scalar) readonly buffer NrmBuf
{
    vec4 n[]; // per-corner normals, count = triCount*3
};

layout(buffer_reference, scalar) readonly buffer UvBuf
{
    vec4 uv[]; // per-corner uvs, count = triCount*3
};

layout(buffer_reference, scalar) readonly buffer MatIdBuf
{
    uint m[];  // per-triangle material ids, count = triCount
};

// ------------------------------------------------------------
// Per-instance data (MUST match CPU RtInstanceData exactly)
// ------------------------------------------------------------
struct RtInstanceData
{
    uint64_t posAdr;     // vec4 positions
    uint64_t idxAdr;     // padded uvec4 triangle indices (uint stream)
    uint64_t nrmAdr;     // vec4 corner normals (3 per tri)
    uint64_t uvAdr;      // vec4 corner uvs     (3 per tri)
    uint64_t matIdAdr;   // uint32 per tri      (1 per tri)
    uint     triCount;   // number of triangles
    uint     _pad0;
    uint     _pad1;
    uint     _pad2;
};

layout(set = 0, binding = 4, std430) readonly buffer Instances
{
    RtInstanceData inst[];
} u_inst;

// ------------------------------------------------------------
// SAME material struct + SSBO as raster shaded mode
// (set=1, binding=0)
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

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
float saturate(float x) { return clamp(x, 0.0, 1.0); }

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

    // --------------------------------------------------------
    // Indices (padded uvec4 per tri -> stride 4 uints)
    // --------------------------------------------------------
    const uint base4 = prim * 4u;

    const uint ia = ind.idx[base4 + 0u];
    const uint ib = ind.idx[base4 + 1u];
    const uint ic = ind.idx[base4 + 2u];

    vec3 a = pos.p[ia].xyz;
    vec3 b = pos.p[ib].xyz;
    vec3 c = pos.p[ic].xyz;

    // --------------------------------------------------------
    // Barycentrics
    // --------------------------------------------------------
    const float b1 = attribs.x;
    const float b2 = attribs.y;
    const float b0 = 1.0 - b1 - b2;

    // --------------------------------------------------------
    // Per-corner normals/uvs (stride 3 per tri)
    // --------------------------------------------------------
    const uint base3 = prim * 3u;

    vec3 n0 = nrm.n[base3 + 0u].xyz;
    vec3 n1 = nrm.n[base3 + 1u].xyz;
    vec3 n2 = nrm.n[base3 + 2u].xyz;

    vec3 N = normalize(n0 * b0 + n1 * b1 + n2 * b2);
    if (any(isnan(N)) || dot(N, N) < 1e-20)
        N = normalize(cross(b - a, c - a));

    // (UV still computed, unused for now)
    vec2 uv0 = uvb.uv[base3 + 0u].xy;
    vec2 uv1 = uvb.uv[base3 + 1u].xy;
    vec2 uv2 = uvb.uv[base3 + 2u].xy;
    vec2 UV  = uv0 * b0 + uv1 * b1 + uv2 * b2;

    // --------------------------------------------------------
    // Material base color (same as raster)
    // --------------------------------------------------------
    const uint matIdRaw = mid.m[prim];

    // Safe clamp like raster
    const uint matCount = materials.length();
    const uint matId    = (matCount > 0u) ? min(matIdRaw, matCount - 1u) : 0u;

    vec3 albedo = (matCount > 0u) ? materials[matId].baseColor : vec3(1.0, 0.0, 1.0);

    // --------------------------------------------------------
    // Simple lighting (keep it minimal for now)
    // --------------------------------------------------------
    vec3 L = normalize(vec3(0.35, 0.85, 0.25));
    vec3 V = normalize(-gl_WorldRayDirectionEXT);

    float ndl = max(dot(N, L), 0.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0) * 0.25;

    vec3 col = albedo * (0.12 + 0.88 * ndl) + rim;
    payload = vec4(col, 1.0);
}
