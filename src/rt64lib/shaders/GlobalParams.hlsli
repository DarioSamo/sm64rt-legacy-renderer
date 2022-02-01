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
    float4 ambientFogColor;
    float4 groundFogColor;
    float2 ambientFogFactors;
    float2 groundFogFactors;
    float2 groundFogHeightFactors;
	float2 pixelJitter;
	float skyYawOffset;
	float giDiffuseStrength;
	float giSkyStrength;
    float motionBlurStrength;
    float tonemapExposure;
    float tonemapWhite;
    float tonemapBlack;
    float tonemapSaturation;
    float tonemapGamma;
	int skyPlaneTexIndex;
	uint randomSeed;
	uint diSamples;
	uint giSamples;
	uint diReproject;
	uint giReproject;
    uint maxLights;
    uint tonemapMode;
	uint motionBlurSamples;
    uint visualizationMode;
    uint frameCount;
    uint volumetricEnabled;
    uint volumetricMaxSamples;
}

#define VISUALIZATION_MODE_FINAL					0
#define VISUALIZATION_MODE_SHADING_POSITION			1
#define VISUALIZATION_MODE_SHADING_NORMAL			2
#define VISUALIZATION_MODE_SHADING_SPECULAR			3
#define VISUALIZATION_MODE_DIFFUSE					4
#define VISUALIZATION_MODE_INSTANCE_ID				5
#define VISUALIZATION_MODE_DIRECT_LIGHT_RAW			6
#define VISUALIZATION_MODE_DIRECT_LIGHT_FILTERED	7
#define VISUALIZATION_MODE_INDIRECT_LIGHT_RAW		8
#define VISUALIZATION_MODE_INDIRECT_LIGHT_FILTERED	9
#define VISUALIZATION_MODE_REFLECTION				10
#define VISUALIZATION_MODE_REFRACTION				11
#define VISUALIZATION_MODE_TRANSPARENT				12
#define VISUALIZATION_MODE_FLOW						13
#define VISUALIZATION_MODE_DEPTH					14
#define VISUALIZATION_MODE_VOLUMETRICS_RAW			15
#define VISUALIZATION_MODE_VOLUMETRICS_FILTERED		16
//)raw"
#endif