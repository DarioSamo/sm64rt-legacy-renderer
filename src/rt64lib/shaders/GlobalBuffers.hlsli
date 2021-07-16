//
// RT64
//

RWTexture2D<float4> gViewDirection : register(u0);
RWTexture2D<float4> gShadingPosition : register(u1);
RWTexture2D<float4> gShadingNormal : register(u2);
RWTexture2D<float4> gShadingSpecular : register(u3);
RWTexture2D<float4> gDiffuse : register(u4);
RWTexture2D<int> gInstanceId : register(u5);
RWTexture2D<float4> gDirectLight : register(u6);
RWTexture2D<float4> gIndirectLight : register(u7);
RWTexture2D<float4> gReflection : register(u8);
RWTexture2D<float4> gRefraction : register(u9);
RWTexture2D<float4> gTransparent : register(u10);
RWTexture2D<float4> gFlow : register(u11);

Texture2D<float4> gBackground : register(t1);