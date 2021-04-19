//
// RT64
//

RWTexture2D<float4> gOutput : register(u0);
RWBuffer<float4> gColor : register(u1);
RWBuffer<float4> gAlbedo : register(u2);
RWBuffer<float4> gNormal : register(u3);
RWBuffer<float4> gDenoised : register(u4);

Texture2D<float4> gBackground : register(t1);
Texture2D<float4> gForeground : register(t2);