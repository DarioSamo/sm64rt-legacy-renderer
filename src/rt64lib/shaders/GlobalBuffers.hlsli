//
// RT64
//

RWTexture2D<float4> gViewDirection : register(u0);
RWTexture2D<float4> gShadingPosition : register(u1);
RWTexture2D<float4> gShadingNormal : register(u2);
RWTexture2D<float4> gShadingSpecular : register(u3);
RWTexture2D<float4> gShadingEmissive : register(u4);
RWTexture2D<float4> gDiffuse : register(u5);
RWTexture2D<int> gInstanceId : register(u6);
RWTexture2D<float4> gDirectLightAccum : register(u7);
RWTexture2D<float4> gIndirectLightAccum : register(u8);
RWTexture2D<float4> gVolumetrics : register(u9);
RWTexture2D<float4> gReflection : register(u10);
RWTexture2D<float4> gRefraction : register(u11);
RWTexture2D<float4> gTransparent : register(u12);
RWTexture2D<float2> gFlow : register(u13);
RWTexture2D<float4> gNormal : register(u14);
RWTexture2D<float> gDepth : register(u15);
RWTexture2D<float4> gPrevNormal : register(u16);
RWTexture2D<float> gPrevDepth : register(u17);
RWTexture2D<float4> gPrevDirectLightAccum : register(u18);
RWTexture2D<float4> gPrevIndirectLightAccum : register(u19);
RWTexture2D<float4> gFilteredDirectLight : register(u20);
RWTexture2D<float4> gFilteredIndirectLight : register(u21);
RWTexture2D<float4> gFog : register(u22);
RWTexture2D<float4> gSpecularLightAccum : register(u23);

Texture2D<float4> gBackground : register(t1);