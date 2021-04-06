//
// RT64
//

#include "Instances.hlsli"
#include "Samplers.hlsli"
#include "Textures.hlsli"
#include "PSInput.hlsli"

int instanceIndex : register(b0);

struct VSInput {
    float4 position : POSITION;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 input1 : COLOR0;
    float4 input2 : COLOR1;
    float4 input3 : COLOR2;
    float4 input4 : COLOR3;
};

PSInput VSMain(VSInput input) {
    PSInput result;
    result.position = input.position;
    result.normal = input.normal;
    result.uv = input.uv;
    result.input1 = input.input1;
    result.input2 = input.input2;
    result.input3 = input.input3;
    result.input4 = input.input4;
    return result;
}