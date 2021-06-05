//
// RT64
//

#include "Textures.hlsli"
#include "ViewParams.hlsli"

SamplerState skyPlaneSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    return gTextures[skyPlaneTexIndex].SampleLevel(skyPlaneSampler, uv, 0);
}
