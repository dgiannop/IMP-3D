//==============================================================
// RtShadow.rchit  (Shadow closest-hit: occluded)
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT uint occFlag;

void main()
{
    occFlag = 1u;
}
