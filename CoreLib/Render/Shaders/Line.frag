//==============================================================
// Line.frag
//==============================================================
#version 450

layout(location = 0) out vec4 outColor;

// Push constants: only color used here
layout(push_constant) uniform PC
{
    mat4 model;
    vec4 color;
} pc;

void main()
{
    outColor = pc.color;
}
