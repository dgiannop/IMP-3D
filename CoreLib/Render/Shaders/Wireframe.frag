//==============================================================
// Wireframe.frag
//==============================================================
#version 450

layout(location = 0) out vec4 outColor;

// Must match Renderer::PushConstants layout exactly
layout(push_constant) uniform PC
{
    mat4 model;
    vec4 color;
    vec4 overlayParams; // unused here
} pc;

void main()
{
    outColor = pc.color;
}
