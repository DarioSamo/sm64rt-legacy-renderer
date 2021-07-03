//
// RT64
//

Texture2D<float4> gOutput : register(t0);
SamplerState gOutputSampler : register(s0);

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    return gOutput.SampleLevel(gOutputSampler, uv, 0);
}