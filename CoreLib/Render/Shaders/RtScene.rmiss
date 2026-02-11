//==============================================================
// RtScene.rmiss
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

// Same CameraUBO: set = 0, binding = 0
layout(set = 0, binding = 0, std140) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    mat4 viewProj;

    mat4 invProj;
    mat4 invView;
    mat4 invViewProj;

    vec4 camPos;
    vec4 viewport;
    vec4 clearColor;
} uCamera;

void main()
{
    payload = uCamera.clearColor;
}
