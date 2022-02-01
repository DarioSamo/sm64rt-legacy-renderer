/*
*
*   Bicubic Upscaling
*   Adapted from https://www.shadertoy.com/view/XtKfRV
*
*/

#include "Color.hlsli"
#define BLOCK_SIZE 8

Texture2D<float4> gInput : register(t0);
RWTexture2D<float4> gOutput : register(u0);
SamplerState gSampler : register(s0);

cbuffer BicubicCB : register(b0)
{
    uint2 InputResolution;
    uint2 OutputResolution;
}

float4 cubic(float x)
{
    float x2 = x * x;
    float x3 = x2 * x;
    float4 w;
    w.x = -x3 + 3.0 * x2 - 3.0 * x + 1.0;
    w.y = 3.0 * x3 - 6.0 * x2 + 4.0;
    w.z = -3.0 * x3 + 3.0 * x2 + 3.0 * x + 1.0;
    w.w = x3;
    return w / 6.0;
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void mainCS(uint2 coord : SV_DispatchThreadID)
{
    float4 resColor = float4(0, 0, 0, 0);
    float fx = -0.5;
    float fy = -0.5;
    float4 xcubic = cubic(fx);
    float4 ycubic = cubic(fy);

    float4 c = float4(coord.x - 0.5, coord.x + 1.5, coord.y - 0.5, coord.y + 1.5);
    float4 s = float4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
    float4 offset = c + float4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

    float4 sample0 = gInput.SampleLevel(gSampler, float2(offset.x, offset.z) / OutputResolution, 0);
    float4 sample1 = gInput.SampleLevel(gSampler, float2(offset.y, offset.z) / OutputResolution, 0);
    float4 sample2 = gInput.SampleLevel(gSampler, float2(offset.x, offset.w) / OutputResolution, 0);
    float4 sample3 = gInput.SampleLevel(gSampler, float2(offset.y, offset.w) / OutputResolution, 0);

    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);
    resColor += lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);
    gOutput[coord].rgb += BlendAOverB(gOutput[coord], resColor).rgb; 
    gOutput[coord].a *= resColor.a;
}