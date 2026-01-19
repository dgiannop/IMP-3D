#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC
{
    layout(offset = 0)  mat4 model;
    layout(offset = 64) vec4 color; // rgb = tint, a = overall opacity
} pc;

void main()
{
    vec2 p = gl_PointCoord * 2.0 - 1.0; // -1..1
    float r = length(p);

    if (r > 1.0)
        discard;

    float fill = 1.0 - smoothstep(0.7, 1.0, r);

    float ringInner = 0.8;
    float ringOuter = 1.0;
    float ring = smoothstep(ringInner, ringOuter, r) -
                 smoothstep(ringOuter, ringOuter + 0.05, r);

    vec3 fillColor   = pc.color.rgb; // tint from push constant
    vec3 borderColor = vec3(1.0);    // keep ring white

    vec3  color = mix(fillColor, borderColor, ring);
    float alpha = max(fill, ring) * pc.color.a;

    outColor = vec4(color, alpha);
}
