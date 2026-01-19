#version 450

layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4  vColor[];
layout(location = 1) in float vThickness[];

layout(location = 0) out vec4 gColor;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
    layout(offset = 80) vec4 overlayParams; // xy = viewport px
} pc;

void main()
{
    vec2 vpSize = pc.overlayParams.xy;
    vpSize = max(vpSize, vec2(1.0));

    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;

    // NDC positions
    vec2 n0 = p0.xy / max(p0.w, 1e-6);
    vec2 n1 = p1.xy / max(p1.w, 1e-6);

    vec2 d = n1 - n0;
    float len = length(d);
    if (len < 1e-8)
        return;

    vec2 dir  = d / len;
    vec2 perp = vec2(-dir.y, dir.x);

    float thicknessPx = max(vThickness[0], 1.0);
    float halfPx      = 0.5 * thicknessPx;

    // Convert pixel offset -> NDC offset
    vec2 ndcPerPixel = vec2(2.0 / vpSize.x, 2.0 / vpSize.y);
    vec2 offNdc = perp * halfPx * ndcPerPixel;

    // Convert NDC offset back to clip by multiplying by w per-endpoint
    vec4 o0 = vec4(offNdc * p0.w, 0.0, 0.0);
    vec4 o1 = vec4(offNdc * p1.w, 0.0, 0.0);

    gColor = vColor[0];
    gl_Position = p0 + o0; EmitVertex();
    gl_Position = p0 - o0; EmitVertex();

    gColor = vColor[1];
    gl_Position = p1 + o1; EmitVertex();
    gl_Position = p1 - o1; EmitVertex();

    EndPrimitive();
}
