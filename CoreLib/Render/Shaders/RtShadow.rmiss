//==============================================================
// RtShadow.rmiss
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT uint shadowHit;

void main()
{
    // Shadow ray missed everything => not occluded
    shadowHit = 0u;
}
