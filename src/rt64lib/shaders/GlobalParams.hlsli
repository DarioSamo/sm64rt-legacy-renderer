//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
cbuffer gParams : register(b0) {
	float4x4 view;
	float4x4 projection;
	float4x4 viewI;
	float4x4 projectionI;
	float4x4 prevViewProj;
	float4 viewport;
	float4 resolution;
	float4 ambientLightColor;
	float4 eyeLightDiffuseColor;
	float4 eyeLightSpecularColor;
	float4 skyHSLModifier;
	float giDiffuseStrength;
	float giSkyStrength;
	int skyPlaneTexIndex;
	uint randomSeed;
	uint softLightSamples;
	uint giBounces;
	uint maxLightSamples;
	uint visualizationMode;
	uint frameCount;
}
//)raw"
#endif