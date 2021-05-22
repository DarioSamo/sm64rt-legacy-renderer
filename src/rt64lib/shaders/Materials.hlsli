//
// RT64
//

struct MaterialProperties {
	int diffuseTexIndex;
	int normalTexIndex;
	int specularTexIndex;
	float ignoreNormalFactor;
	float uvDetailScale;
	float reflectionFactor;
	float reflectionFresnelFactor;
	float reflectionShineFactor;
	float refractionFactor;
	float specularIntensity;
	float specularExponent;
	float solidAlphaMultiplier;
	float shadowAlphaMultiplier;
	float depthBias;
	float shadowRayBias;
	float3 selfLight;
	uint lightGroupMaskBits;
	float3 fogColor;
	float4 diffuseColorMix;
	float fogMul;
	float fogOffset;
	uint fogEnabled;
	uint3 _reserved;
};