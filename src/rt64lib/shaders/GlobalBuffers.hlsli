//
// RT64
//

RWTexture2D<float4> gViewDirection : register(u0);
RWTexture2D<float4> gShadingPosition : register(u1);
RWTexture2D<float4> gShadingNormal : register(u2);
RWTexture2D<float4> gShadingSpecular : register(u3);
RWTexture2D<float4> gDiffuse : register(u4);
RWTexture2D<int> gInstanceId : register(u5);
RWTexture2D<float4> gDirectLightAccum : register(u6);
RWTexture2D<float4> gIndirectLightAccum : register(u7);
RWTexture2D<float4> gVolumetrics : register(u8);
RWTexture2D<float4> gReflection : register(u9);
RWTexture2D<float4> gRefraction : register(u10);
RWTexture2D<float4> gTransparent : register(u11);
RWTexture2D<float2> gFlow : register(u12);
RWTexture2D<float4> gNormal : register(u13);
RWTexture2D<float> gDepth : register(u14);
RWTexture2D<float4> gPrevNormal : register(u15);
RWTexture2D<float> gPrevDepth : register(u16);
RWTexture2D<float4> gPrevDirectLightAccum : register(u17);
RWTexture2D<float4> gPrevIndirectLightAccum : register(u18);
RWTexture2D<float4> gFilteredDirectLight : register(u19);
RWTexture2D<float4> gFilteredIndirectLight : register(u20);
RWTexture2D<float4> gFog : register(u21);

Texture2D<float4> gBackground : register(t1);