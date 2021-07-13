//
// RT64
//

RWTexture2D<float4> gShadingPosition : register(u0);
RWTexture2D<float4> gShadingNormal : register(u1);
RWTexture2D<float4> gShadingSpecular : register(u2);
RWTexture2D<float4> gDiffuse : register(u3);
RWTexture2D<int> gInstanceId : register(u4);
RWTexture2D<float4> gDirectLight : register(u5);
RWTexture2D<float4> gIndirectLight : register(u6);
RWTexture2D<float4> gReflection : register(u7);
RWTexture2D<float4> gRefraction : register(u8);
RWTexture2D<float4> gFlow : register(u9);

Texture2D<float4> gBackground : register(t1);