//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
struct InstanceTransforms {
	float4x4 objectToWorld;
	float4x4 objectToWorldNormal;
	float4x4 objectToWorldPrevious;
};

StructuredBuffer<InstanceTransforms> instanceTransforms : register(t5);
StructuredBuffer<MaterialProperties> instanceMaterials : register(t6);

float WithDistanceBias(float distance, uint instanceId) {
	return distance - instanceMaterials[instanceId].depthBias;
}

float WithoutDistanceBias(float distance, uint instanceId) {
	return distance + instanceMaterials[instanceId].depthBias;
}
//)raw"
#endif