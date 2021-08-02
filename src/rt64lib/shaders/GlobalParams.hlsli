//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
cbuffer gParams : register(b0) {
	float4x4 view;
	float4x4 viewI;
	float4x4 prevViewI;
	float4x4 projection;
	float4x4 projectionI;
	float4x4 viewProj;
	float4x4 prevViewProj;
	float4 cameraU;
	float4 cameraV;
	float4 cameraW;
	float4 viewport;
	float4 resolution;
	float4 ambientBaseColor;
	float4 ambientNoGIColor;
	float4 eyeLightDiffuseColor;
	float4 eyeLightSpecularColor;
	float4 skyDiffuseMultiplier;
	float4 skyHSLModifier;
	float2 pixelJitter;
	float skyYawOffset;
	float giDiffuseStrength;
	float giSkyStrength;
	float motionBlurStrength;
	int skyPlaneTexIndex;
	uint randomSeed;
	uint softLightSamples;
	uint giBounces;
	uint maxLightSamples;
	unsigned int motionBlurSamples;
	uint visualizationMode;
	uint frameCount;
}

#define VISUALIZATION_MODE_FINAL			0
#define VISUALIZATION_MODE_SHADING_POSITION	1
#define VISUALIZATION_MODE_SHADING_NORMAL	2
#define VISUALIZATION_MODE_SHADING_SPECULAR	3
#define VISUALIZATION_MODE_DIFFUSE			4
#define VISUALIZATION_MODE_INSTANCE_ID		5
#define VISUALIZATION_MODE_DIRECT_LIGHT		6
#define VISUALIZATION_MODE_INDIRECT_LIGHT	7
#define VISUALIZATION_MODE_REFLECTION		8
#define VISUALIZATION_MODE_REFRACTION		9
#define VISUALIZATION_MODE_TRANSPARENT		10
#define VISUALIZATION_MODE_FLOW				11
#define VISUALIZATION_MODE_DEPTH			12
//)raw"
#endif