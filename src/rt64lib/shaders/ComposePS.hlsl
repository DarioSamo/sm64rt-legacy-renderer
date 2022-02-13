//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalParams.hlsli"
#include "BicubicFiltering.hlsli"

Texture2D<float4> gFlow : register(t0);
Texture2D<float4> gDiffuse : register(t1);
Texture2D<float4> gDirectLight : register(t2);
Texture2D<float4> gIndirectLight : register(t3);
Texture2D<float4> gVolumetrics : register(t4);
Texture2D<float4> gReflection : register(t5);
Texture2D<float4> gRefraction : register(t6);
Texture2D<float4> gTransparent : register(t7);
Texture2D<float4> gFog : register(t8);
Texture2D<float4> gSpecularLight : register(t9);
Texture2D<float> gAmbientOcclusion : register(t10);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float4 diffuse = gDiffuse.SampleLevel(gSampler, uv, 0);
    if (diffuse.a > EPSILON) {
        float3 directLight = gDirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 specularLight = gSpecularLight.SampleLevel(gSampler, uv, 0).rgb;
        float3 indirectLight = gIndirectLight.SampleLevel(gSampler, uv, 0).rgb;
        float ambient = saturate(gAmbientOcclusion.SampleLevel(gSampler, uv, 0) + RGBtoLuminance(directLight + specularLight + indirectLight));
        indirectLight *= ambient;
        float3 reflection = gReflection.SampleLevel(gSampler, uv, 0).rgb;
        float3 refraction = gRefraction.SampleLevel(gSampler, uv, 0).rgb;
        float3 transparent = gTransparent.SampleLevel(gSampler, uv, 0).rgb;
        float4 volumetrics = BicubicFilter(gVolumetrics, gSampler, uv, resolution.xy);
        float4 fog = gFog.SampleLevel(gSampler, uv, 0);
        fog.rgb += BlendAOverB(fog, volumetrics).rgb;
        fog.a *= volumetrics.a;
        //float4 result = float4(diffuse.rgb, 1.f);
        float4 result = float4(diffuse.rgb, 1.f);
        
        result.rgb *= (directLight + indirectLight);
        result.rgb += specularLight;
        result.rgb = lerp(diffuse.rgb, result.rgb, diffuse.a);
        result.rgb += reflection;
        result.rgb += refraction;
        result.rgb += transparent;
        result = BlendAOverB(fog, result);
        return result;
    }
    else
    {
        float4 volumetrics = BicubicFilter(gVolumetrics, gSampler, uv, resolution.xy);
        float4 fog = gFog.SampleLevel(gSampler, uv, 0);
        fog.rgb += BlendAOverB(fog, volumetrics).rgb;
        fog.a *= volumetrics.a;
        float4 result = float4(diffuse.rgb, 1.f);
        return BlendAOverB(fog, result);
    }
}