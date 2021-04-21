//
// RT64
//

RWTexture2D<float4> gOutput : register(u0);
RWTexture2D<float4> gAlbedo : register(u1);
RWTexture2D<float4> gNormal : register(u2);

Texture2D<float4> gBackground : register(t1);
Texture2D<float4> gForeground : register(t2);