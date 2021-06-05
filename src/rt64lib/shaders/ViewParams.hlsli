//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
cbuffer ViewParams : register(b0) {
	float4x4 view;
	float4x4 projection;
	float4x4 viewI;
	float4x4 projectionI;
	float4x4 prevViewProj;
	float4 viewport;
	float4 resolution;
	int skyPlaneTexIndex;
	uint randomSeed;
	uint softLightSamples;
	uint giBounces;
	uint maxLightSamples;
	float ambGIMixWeight;
	uint frameCount;
}
//)raw"
#endif