//
// RT64
//

#include "Color.hlsli"
#include "Textures.hlsli"
#include "ViewParams.hlsli"

SamplerState skyPlaneSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET{
    float4 color = gTextures[skyPlaneTexIndex].SampleLevel(skyPlaneSampler, uv, 0);
    if (any(skyHSLModifier)) {
        color.rgb = ModRGBWithHSL(color.rgb, skyHSLModifier);
    }

    return color;
}
