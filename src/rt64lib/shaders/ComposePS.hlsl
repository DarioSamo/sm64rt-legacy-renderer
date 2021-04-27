//
// RT64
//

#include "Samplers.hlsli"

Texture2D<float4> gOutput : register(t0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    return gOutput.SampleLevel(linearClampClamp, uv, 0);
}
