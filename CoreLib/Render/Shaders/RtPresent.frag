//==============================================================
// RtPresent.frag
//==============================================================
#version 460

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

// For the RtPresent pipeline, m_rtSetLayout is bound as *set 0*
layout(set = 0, binding = 1) uniform sampler2D u_img;

void main()
{
    o_color = texture(u_img, v_uv);
}
