//==============================================================
// RtShadow.rchit
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require

// Shadow payload: 0 = no hit, 1 = hit
layout(location = 1) rayPayloadInEXT uint shadowHit;

void main()
{
    shadowHit = 1u;
}
