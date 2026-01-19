#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color;
} pc;

void main()
{
    outColor = pc.color;
}
