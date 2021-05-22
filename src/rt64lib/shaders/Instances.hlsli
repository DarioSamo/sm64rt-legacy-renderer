//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
struct InstanceTransforms {
	float4x4 objectToWorld;
	float4x4 objectToWorldNormal;
};

static const float InstanceIdBias = 0.001f;

StructuredBuffer<InstanceTransforms> instanceTransforms : register(t5);
StructuredBuffer<MaterialProperties> instanceMaterials : register(t6);

float WithDistanceBias(float distance, uint instanceId) {
	float depthBias = instanceMaterials[instanceId].depthBias;
	return distance - (instanceId * InstanceIdBias) - depthBias;
}

float WithoutDistanceBias(float distance, uint instanceId) {
	float depthBias = instanceMaterials[instanceId].depthBias;
	return distance + (instanceId * InstanceIdBias) + depthBias;
}
//)raw"
#endif