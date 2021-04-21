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

float WithDistanceBias(float distance, uint instanceId) {
	return max(distance - (instanceId * InstanceIdBias), 0.01);
}

float WithoutDistanceBias(float distance, uint instanceId) {
	return distance + (instanceId * InstanceIdBias);
}

StructuredBuffer<InstanceProperties> instanceProps : register(t5);