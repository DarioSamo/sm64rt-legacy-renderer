//
// RT64
//

// Structures

struct LightInfo {
	float3 position;
	float3 diffuseColor;
	float attenuationRadius;
	float pointRadius;
	float specularIntensity;
	float shadowOffset;
	float attenuationExponent;
	float flickerIntensity;
	uint groupBits;
	uint minSamples;
	uint maxSamples;
};

// Root signature

StructuredBuffer<LightInfo> SceneLights : register(t4);