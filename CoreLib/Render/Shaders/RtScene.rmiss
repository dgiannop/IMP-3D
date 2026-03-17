#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

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

layout(set = 2, binding = 4, rgba16f) uniform image2D u_outNormal;
layout(set = 2, binding = 5, rgba16f) uniform image2D u_outDepth;
layout(set = 2, binding = 6, rgba16f) uniform image2D u_outAlbedo;

void main()
{
    payload = uCamera.clearColor;

    ivec2 pix = ivec2(gl_LaunchIDEXT.xy);

    imageStore(u_outNormal, pix, vec4(0.5, 0.5, 1.0, 0.0));
    imageStore(u_outDepth,  pix, vec4(1e6, 0.0, 0.0, 0.0));
    imageStore(u_outAlbedo, pix, vec4(uCamera.clearColor.rgb, 1.0));
}
