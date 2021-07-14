//
// RT64
//

#include "Constants.hlsli"
#include "GlobalParams.hlsli"

Texture2D<float4> gFlow : register(t0);
Texture2D<float4> gShadingPosition : register(t1);
Texture2D<float4> gDiffuse : register(t2);
Texture2D<int> gInstanceId : register(t3);
Texture2D<float4> gDirectLight : register(t4);
Texture2D<float4> gIndirectLight : register(t5);
Texture2D<float4> gReflection : register(t6);
Texture2D<float4> gRefraction : register(t7);

SamplerState gSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    float4 diffuse = gDiffuse.SampleLevel(gSampler, uv, 0);
    if (diffuse.a > EPSILON) {
        float4 directLight = gDirectLight.SampleLevel(gSampler, uv, 0);
        float4 indirectLight = gIndirectLight.SampleLevel(gSampler, uv, 0);
        float4 reflection = gReflection.SampleLevel(gSampler, uv, 0);
        float4 refraction = gRefraction.SampleLevel(gSampler, uv, 0);
        float3 result = diffuse.rgb;
        
        // Compute the result as affected by the light source.
        result *= (directLight.rgb + indirectLight.rgb);

        // Mix the computed result with the alpha.
        result = lerp(diffuse.rgb, result, diffuse.a);

        // Add the reflection.
        result += reflection.rgb * reflection.a;

        // Add the refraction.
        result += refraction.rgb * refraction.a;

        return float4(result, 1.0f);
    }
    else {
        return float4(diffuse.rgb, 1.0f);
    }
}