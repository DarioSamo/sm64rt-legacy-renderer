//
// RT64
//

#include "Materials.hlsli"

struct InstanceTransforms {
	float4x4 objectToWorld;
	float4x4 objectToWorldNormal;
};

struct InstanceMaterials {
	MaterialProperties materialProperties;
};

static const float InstanceIdBias = 0.001f;

StructuredBuffer<InstanceTransforms> instanceTransforms : register(t5);
StructuredBuffer<InstanceMaterials> instanceMaterials : register(t6);

float WithDistanceBias(float distance, uint instanceId) {
	float depthBias = instanceMaterials[instanceId].materialProperties.depthBias;
	return distance - (instanceId * InstanceIdBias) - depthBias;
}

float WithoutDistanceBias(float distance, uint instanceId) {
	float depthBias = instanceMaterials[instanceId].materialProperties.depthBias;
	return distance + (instanceId * InstanceIdBias) + depthBias;
}
