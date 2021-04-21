//
// RT64
//

#include "Samplers.hlsli"

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gForeground : register(t1);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float3 outColor = gOutput.SampleLevel(linearClampClamp, uv, 0).rgb;
    float4 fgColor = gForeground.SampleLevel(pointClampClamp, uv, 0);
    return float4(lerp(outColor, fgColor.rgb, fgColor.a), 1.0f);
}
