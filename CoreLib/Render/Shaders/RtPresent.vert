//==============================================================
// RtPresent.vert
//==============================================================
#version 460

layout(location = 0) out vec2 v_uv;

vec2 verts[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 p = verts[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);

    // Maps clip-space fullscreen triangle to [0..1] UVs.
    v_uv = 0.5 * (p + 1.0);
}
