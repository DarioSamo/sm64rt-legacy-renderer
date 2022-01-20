//
// RT64
//

#include "Constants.hlsli"
#include "GlobalParams.hlsli"

#define REINHARD 0

Texture2D<float4> gOutput : register(t0);
Texture2D<float4> gFlow : register(t1);

SamplerState gSampler : register(s0);

// Standard clamped tonemapping
float3 Clamped(float3 color, float xp)
{
	return clamp(color * xp, 0.0f, 1.0f);
}

// Reinhard and Luminance-Based Reinhard
// Taken from https://64.github.io/tonemapping/
float3 Reinhard(float3 color, float xp)
{
    float3 num = color * (1.0f + (color / (xp * xp)));
	return num / (1.0f + color);
}

float Luma(float3 color)
{
	return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ChangeLuma(float3 color, float lumaOut)
{
	float lumaIn = Luma(color);
	return color * (lumaOut / lumaIn);
}

float3 ReinhardLuma(float3 color, float xp)
{
	float lumaOld = Luma(color);
	float num = lumaOld * (1.0f + (lumaOld / (xp * xp)));
	float lumaNew = num / (1.0f + lumaOld);
	return ChangeLuma(color, lumaNew);
}

float3 Tonemapping(float3 color, float xp, int mode)
{
    switch (mode)
    {
        case TONEMAP_MODE_CLAMP:
            return Clamped(color, xp);
        case TONEMAP_MODE_REINHARD:
            return Reinhard(color, 1.0f / xp);
        case TONEMAP_MODE_REINHARD_LUMA:
            return ReinhardLuma(color, 1.0f / xp);
    }
	return color * xp;
}

float4 MotionBlur(float4 color, float2 uv)
{
    float2 flow = gFlow.SampleLevel(gSampler, uv, 0).xy / resolution.xy;
    float flowLength = length(flow);
    if (flowLength > 1e-6f)
    {
        const float SampleStep = motionBlurStrength / motionBlurSamples;
        float4 sumColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float sumWeight = 0.0f;
        float2 startUV = uv - (flow * motionBlurStrength / 2.0f);
        for (uint s = 0; s < motionBlurSamples; s++)
        {
            float2 sampleUV = clamp(startUV + flow * s * SampleStep, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
            float sampleWeight = 1.0f;
            sumColor += gOutput.SampleLevel(gSampler, sampleUV, 0) * sampleWeight;
            sumWeight += sampleWeight;
        }

        color = sumColor / sumWeight;
    }
    return color;
}

float AutoExposure()
{
    
}

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET
{
	float4 color = gOutput.SampleLevel(gSampler, uv, 0);
	if ((motionBlurStrength > 0.0f) && (motionBlurSamples > 0)) {
        color = MotionBlur(color, uv);
    }
	
    float expOffset = tex2Dlod(gSampler, float4(0.5, 0.5, 0, 0.25));
    color = float4(Tonemapping(color.rgb, tonemapExposure, tonemapMode), color.w);
    return color;
}