//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
struct MaterialProperties {
	int diffuseTexIndex;
	int normalTexIndex;
	int specularTexIndex;
	int emissiveTexIndex;
	int roughnessTexIndex;
	int metalnessTexIndex;
	int ambientTexIndex;
	float ignoreNormalFactor;
	float uvDetailScale;
	float reflectionFactor;
	float reflectionFresnelFactor;
	float reflectionShineFactor;
	float refractionFactor;
	float3 specularColor;
	float specularExponent;
    float roughnessFactor;
    float metallicFactor;
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
	float specularFresnelFactor;
	
	uint _reserved;
};
//)raw"
#endif