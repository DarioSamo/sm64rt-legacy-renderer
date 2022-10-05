//
// RT64
//

#include "Constants.hlsli"
#include "GlobalParams.hlsli"

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    if ((motionBlurStrength > 0.0f) && (motionBlurSamples > 0)) {
        float2 flow = gFlow.SampleLevel(gSampler, uv, 0).xy / resolution.xy;
        float flowLength = length(flow);
        if (flowLength > 1e-6f) {
            const float SampleStep = motionBlurStrength / motionBlurSamples;
            float3 sumColor = float3(0.0f, 0.0f, 0.0f);
            float sumWeight = 0.0f;
            float2 startUV = uv - (flow * motionBlurStrength / 2.0f);
            for (uint s = 0; s < motionBlurSamples; s++) {
                float2 sampleUV = clamp(startUV + flow * s * SampleStep, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
                float sampleWeight = 1.0f;
                float4 outputColor = gOutput.SampleLevel(gSampler, sampleUV, 0);
                sumColor += outputColor.rgb * sampleWeight;
                sumWeight += sampleWeight;
            }

            return float4(sumColor / sumWeight, 1.0f);
        }
    }

    float4 outputColor = gOutput.SampleLevel(gSampler, uv, 0);
    return float4(outputColor.rgb, 1.0f);
}