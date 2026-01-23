#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

layout(set = 0, binding = 2) uniform CameraUBO
{
    mat4 invViewProj;
    vec4 camPos;
    vec4 clearColor;
} cam;

void main()
{
    payload = cam.clearColor;
}


/*
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

void main()
{
    // Simple sky-ish gradient based on ray direction
    vec3 d = normalize(gl_WorldRayDirectionEXT);
    float t = 0.5 * (d.y + 1.0);
    vec3 sky = mix(vec3(0.15, 0.15, 0.18), vec3(0.55, 0.65, 0.85), t);

    payload = vec4(sky, 1.0);
}
*/
