//
// RT64
//

#include "Constants.hlsli"
#include "GlobalParams.hlsli"

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);
Texture2D<float4> gShadingPosition : register(t2);
Texture2D<float4> gDiffuse : register(t3);
Texture2D<int> gInstanceId : register(t4);
Texture2D<float4> gDirectLight : register(t5);
Texture2D<float4> gIndirectLight : register(t6);
Texture2D<float4> gReflection : register(t7);
Texture2D<float4> gRefraction : register(t8);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET{
    float4 diffuse = gDiffuse.SampleLevel(gSampler, uv, 0);
    if (diffuse.a > EPSILON) {
        float4 directLight = gDirectLight.SampleLevel(gSampler, uv, 0);
        float4 indirectLight = gIndirectLight.SampleLevel(gSampler, uv, 0);
        float4 reflection = gReflection.SampleLevel(gSampler, uv, 0);
        float4 refraction = gRefraction.SampleLevel(gSampler, uv, 0);
        float4 result = diffuse;
        result.rgb *= (directLight.rgb + indirectLight.rgb);
        result.rgb += refraction.rgb * refraction.a;
        result.rgb = lerp(result.rgb, reflection.rgb, reflection.a);
        return float4(lerp(diffuse.rgb, result.rgb, result.a), 1.0f);
    }
    else {
        return float4(diffuse.rgb, 1.0f);
    }

    /*
    if ((motionBlurStrength > 0.0f) && (motionBlurSamples > 0)) {
        float2 flow = gFlow.SampleLevel(gSampler, uv, 0).xy / resolution.xy;
        float flowLength = length(flow);
        if (flowLength > 1e-6f) {
            const float SampleStep = motionBlurStrength / motionBlurSamples;
            float4 sumColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
            float sumWeight = 0.0f;
            float2 startUV = uv - (flow * motionBlurStrength / 2.0f);
            for (uint s = 0; s < motionBlurSamples; s++) {
                float2 sampleUV = clamp(startUV + flow * s * SampleStep, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
                float sampleWeight = 1.0f;
                sumColor += gOutput.SampleLevel(gSampler, sampleUV, 0) * sampleWeight;
                sumWeight += sampleWeight;
            }

            return sumColor / sumWeight;
        }
    }

    return gOutput.SampleLevel(gSampler, uv, 0);
    */
}