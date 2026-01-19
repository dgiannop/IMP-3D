#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;

// ------------------------------------------------------------
// Buffer references
// ------------------------------------------------------------

// vec4 avoids vec3 runtime-array stride/alignment issues
layout(buffer_reference, scalar) readonly buffer PosBuf
{
    vec4 p[];
};

// NOTE: this is the "tight" index list (3 per tri) OR your shader-readable uvec4-per-tri.
// Your current shader assumes tight (3 per tri). Keep it that way if that's what idxAdr points to.
layout(buffer_reference, scalar) readonly buffer IdxBuf
{
    uint idx[];
};

layout(buffer_reference, scalar) readonly buffer NrmBuf
{
    vec4 n[]; // xyz = normal, w unused
};

layout(buffer_reference, scalar) readonly buffer UvBuf
{
    vec4 uv[]; // xy = uv, zw unused
};

// ------------------------------------------------------------
// Per-instance data (matches CPU exactly)
// ------------------------------------------------------------
struct RtInstanceData
{
    uint64_t posAdr;   // vec4 positions
    uint64_t idxAdr;   // uint triangle indices (3 per tri)
    uint64_t nrmAdr;   // vec4 corner normals (3 per tri)
    uint64_t uvAdr;    // vec4 corner uvs     (3 per tri)  <-- NEW
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
// Main
// ------------------------------------------------------------
void main()
{
    const uint instId = gl_InstanceCustomIndexEXT;
    RtInstanceData d = u_inst.inst[instId];
/*
uint i = gl_InstanceCustomIndexEXT;
payload = vec4(float(i & 1u), float((i >> 1u) & 1u), float((i >> 2u) & 1u), 1.0);
return;

// Color-code missing pieces (pick any scheme you like)
if (d.posAdr == 0ul) { payload = vec4(1,0,0,1); return; } // red = no positions
if (d.idxAdr == 0ul) { payload = vec4(0,1,0,1); return; } // green = no indices
if (d.nrmAdr == 0ul) { payload = vec4(0,0,1,1); return; } // blue = no normals
payload = vec4(1,1,0,1); return; // yellow = addresses look non-zero
*/
    if (d.posAdr == 0ul || d.idxAdr == 0ul || d.nrmAdr == 0ul /*|| d.uvAdr == 0ul*/ || d.triCount == 0u)
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0); // magenta = invalid instance
        return;
    }

    const uint prim = gl_PrimitiveID;
    if (prim >= d.triCount)
    {
        payload = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    PosBuf pos = PosBuf(d.posAdr);
    IdxBuf ind = IdxBuf(d.idxAdr);
    NrmBuf nrm = NrmBuf(d.nrmAdr);
    UvBuf  uvb = UvBuf(d.uvAdr);

    // --------------------------------------------------------
    // Triangle indices
    // --------------------------------------------------------
    const uint base = prim * 3u;

    const uint ia = ind.idx[base + 0u];
    const uint ib = ind.idx[base + 1u];
    const uint ic = ind.idx[base + 2u];

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
    // Face-varying corner normals (ALWAYS interpolate)
    // --------------------------------------------------------
    vec3 n0 = nrm.n[base + 0u].xyz;
    vec3 n1 = nrm.n[base + 1u].xyz;
    vec3 n2 = nrm.n[base + 2u].xyz;

    vec3 N = normalize(n0 * b0 + n1 * b1 + n2 * b2);
    if (any(isnan(N)) || dot(N, N) < 1e-20)
        N = normalize(cross(b - a, c - a)); // safe fallback

    // --------------------------------------------------------
    // Face-varying corner UVs (interpolate)
    // --------------------------------------------------------
    vec2 uv0 = uvb.uv[base + 0u].xy;
    vec2 uv1 = uvb.uv[base + 1u].xy;
    vec2 uv2 = uvb.uv[base + 2u].xy;

    vec2 UV = uv0 * b0 + uv1 * b1 + uv2 * b2;
    // for later - > texture(sampler, UV).rgb

    // --------------------------------------------------------
    // Shading (no UV influence)
    // --------------------------------------------------------
    vec3 L = normalize(vec3(0.35, 0.85, 0.25));
    vec3 V = normalize(-gl_WorldRayDirectionEXT);

    float ndl = max(dot(N, L), 0.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0) * 0.25;

    // plain base color
    vec3 baseCol = vec3(0.85);

    vec3 col = baseCol * (0.12 + 0.88 * ndl) + rim;
    payload = vec4(col, 1.0);
}
