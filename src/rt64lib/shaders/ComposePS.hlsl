//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalParams.hlsli"

Texture2D<float4> gFlow : register(t0);
Texture2D<float4> gDiffuse : register(t1);
Texture2D<float4> gDirectLight : register(t2);
Texture2D<float4> gIndirectLight : register(t3);
Texture2D<float4> gVolumetricFog : register(t4);
Texture2D<float4> gReflection : register(t5);
Texture2D<float4> gRefraction : register(t6);
Texture2D<float4> gTransparent : register(t7);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float4 diffuse = gDiffuse.SampleLevel(gSampler, uv, 0);
    if (diffuse.a > EPSILON) {
        float3 directLight = gDirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 indirectLight = gIndirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 reflection = gReflection.SampleLevel(gSampler, uv, 0).rgb;
        float3 refraction = gRefraction.SampleLevel(gSampler, uv, 0).rgb;
        float3 transparent = gTransparent.SampleLevel(gSampler, uv, 0).rgb;
        float4 volumetrics = gVolumetricFog.SampleLevel(gSampler, uv, 0);
        float4 result = float4(diffuse.rgb, 1.f);
        result.rgb *= (directLight + indirectLight);
        result.rgb = lerp(diffuse.rgb, result.rgb, diffuse.a);
        result.rgb += reflection;
        result.rgb += refraction;
        result.rgb += transparent;
        result = BlendAOverB(volumetrics, result);
        return result;
    }
    else {
        return float4(diffuse.rgb, 1.0f);
    }
}