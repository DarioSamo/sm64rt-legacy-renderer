//
// RT64
//

#include "GlobalParams.hlsli"

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);
SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    if (motionBlurSamples > 0) {
        const float SampleStep = motionBlurStrength / motionBlurSamples;
        float4 resColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
        for (uint s = 0; s < motionBlurSamples; s++) {
            float2 flow = gFlow.SampleLevel(gSampler, uv, 0).xy / resolution.xy;
            float2 newUV = clamp(uv + flow * s * SampleStep, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
            resColor += gOutput.SampleLevel(gSampler, newUV, 0) / motionBlurSamples;
        }

        return resColor;
    }
    else {
        return gOutput.SampleLevel(gSampler, uv, 0);
    }
}