//==============================================================
// RtScene.rmiss
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

layout(set = 0, binding = 2, std140) uniform RtCameraUBO
{
    mat4 invViewProj;
    mat4 view;
    mat4 invView;
    vec4 camPos;
    vec4 clearColor;
} u_cam;

void main()
{
    payload = u_cam.clearColor;
}
