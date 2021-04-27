//
// RT64
//

#include "Materials.hlsli"
#include "N64CC.hlsli"

struct InstanceProperties {
	float4x4 objectToWorld;
	float4x4 objectToWorldNormal;
	MaterialProperties materialProperties;
	ColorCombinerFeatures ccFeatures;
};

static const float InstanceIdBias = 0.001f;

StructuredBuffer<InstanceProperties> instanceProps : register(t5);

float WithDistanceBias(float distance, uint instanceId) {
	float depthBias = instanceProps[instanceId].materialProperties.depthBias;
	return distance - (instanceId * InstanceIdBias) - depthBias;
}

float WithoutDistanceBias(float distance, uint instanceId) {
	float depthBias = instanceProps[instanceId].materialProperties.depthBias;
	return distance + (instanceId * InstanceIdBias) + depthBias;
}
