//
// RT64
//

RWTexture2D<float4> gOutput : register(u0);
RWTexture2D<float4> gAlbedo : register(u1);
RWTexture2D<float4> gNormal : register(u2);
RWTexture2D<float4> gFlow : register(u3);

RWTexture2D<float4> gShadingPosition : register(u9);
RWTexture2D<float4> gShadingNormal : register(u10);
RWTexture2D<float4> gShadingSpecular : register(u11);
RWTexture2D<float4> gDiffuse : register(u12);
RWTexture2D<int> gInstanceId : register(u13);
RWTexture2D<float4> gDirectLight : register(u14);
RWTexture2D<float4> gIndirectLight : register(u15);
RWTexture2D<float4> gReflection : register(u16);
RWTexture2D<float4> gRefraction : register(u17);

Texture2D<float4> gBackground : register(t1);