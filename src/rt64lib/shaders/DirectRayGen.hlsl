//
// RT64
//

#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Lights.hlsli"

[shader("raygeneration")]
void DirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if (instanceId < 0) {
		gDirectLightAccum[launchIndex] = float4(1.0f, 1.0f, 1.0f, 0.0f);
		return;
	}

	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	float4 position = gShadingPosition[launchIndex];
	float3 normal = gShadingNormal[launchIndex].xyz;
	float4 specular = gShadingSpecular[launchIndex];

	float3 newDirect = float3(0.0f, 0.0f, 0.0f);
	float historyLength = 0.0f;

	// Reproject previous direct.
	if (diReproject) {
		const float WeightNormalExponent = 128.0f;
		float2 flow = gFlow[launchIndex].xy;
		int2 prevIndex = int2(launchIndex + float2(0.5f, 0.5f) + flow);
		float prevDepth = gPrevDepth[prevIndex];
		float3 prevNormal = gPrevNormal[prevIndex].xyz;
		float4 prevDirectAccum = gPrevDirectLightAccum[prevIndex];
		float depth = gDepth[launchIndex];
		float weightDepth = abs(depth - prevDepth) / 0.01f;
		float weightNormal = pow(saturate(dot(prevNormal, normal)), WeightNormalExponent);
		float historyWeight = exp(-weightDepth) * weightNormal;
		newDirect = prevDirectAccum.rgb;
		historyLength = prevDirectAccum.a * historyWeight;
    }
	
    float2x3 lightMatrix = ComputeLightsRandom(launchIndex, rayDirection, instanceId, position.xyz, normal.xyz, specular.xyz, maxLights, instanceMaterials[instanceId].lightGroupMaskBits, instanceMaterials[instanceId].ignoreNormalFactor, true);
    float3 resDirect = lightMatrix._11_12_13 + lightMatrix._21_22_23;
	resDirect += instanceMaterials[instanceId].selfLight;

	// Add the eye light.
	float specularExponent = instanceMaterials[instanceId].specularExponent;
    float eyeLightLambertFactor = saturate(dot(normal.xyz, -(rayDirection)));
    float3 eyeLightReflected = reflect(rayDirection, normal.xyz);
    float3 eyeLightSpecularFactor = specular.rgb * pow(saturate(dot(eyeLightReflected, -(rayDirection))), specularExponent);
	resDirect += (eyeLightDiffuseColor.rgb * eyeLightLambertFactor + eyeLightSpecularColor.rgb * eyeLightSpecularFactor);

	// Accumulate.
	historyLength = min(historyLength + 1.0f, 64.0f);
	newDirect = lerp(newDirect.rgb, resDirect, 1.0f / historyLength);

	gDirectLightAccum[launchIndex] = float4(newDirect, historyLength);
}