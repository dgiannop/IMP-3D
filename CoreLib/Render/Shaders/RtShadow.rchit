//==============================================================
// RtShadow.rchit  (Shadow hit)  — SBT HitGroup[1] (closest-hit)
// Use this if you do NOT have alpha cutouts.
//==============================================================
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT uint occFlag;

void main()
{
    // Any hit -> occluded
    occFlag = 1u;
}
