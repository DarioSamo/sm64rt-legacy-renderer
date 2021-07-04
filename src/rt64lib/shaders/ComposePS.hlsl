//
// RT64
//

#include "GlobalParams.hlsli"

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);
SamplerState gSampler : register(s0);

#define FORCE_MOTION_BLUR     0

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET{
#if FORCE_MOTION_BLUR == 1
    const uint Samples = 64;
    const float SampleStep = 1.0f / Samples;
    float4 resColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint s = 0; s < Samples; s++) {
        float2 flow = gFlow.SampleLevel(gSampler, uv, 0).xy / resolution.xy;
        float2 newUV = clamp(uv + flow * s * SampleStep, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
        resColor += gOutput.SampleLevel(gSampler, newUV, 0) / Samples;
    }

    return resColor;
#else
    return gOutput.SampleLevel(gSampler, uv, 0);
#endif
}