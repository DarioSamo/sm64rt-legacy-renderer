/*
*
*   Bicubic Upscaling
*   Adapted from https://www.shadertoy.com/view/XtKfRV
*
*/

#include "Color.hlsli"
#include "BicubicFiltering.hlsli"
#define BLOCK_SIZE 8

Texture2D<float4> gInput : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer BicubicCB : register(b0)
{
    uint2 InputResolution;
    uint2 OutputResolution;
}

SamplerState gSampler : register(s0);

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void mainCS(uint2 coord : SV_DispatchThreadID) {
    if (coord.x < OutputResolution.x && coord.y < OutputResolution.y) {
        gOutput[coord] = BicubicFilter(gInput, gSampler, float2(coord) / float2(OutputResolution), OutputResolution);
    }
}