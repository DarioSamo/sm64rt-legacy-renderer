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
		gDirectLight[launchIndex] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);
	float4 position = gShadingPosition[launchIndex];
	float4 normal = gShadingNormal[launchIndex];
	float4 specular = gShadingSpecular[launchIndex];
	float3 directLight = ComputeLightsRandom(rayDirection, instanceId, position.xyz, normal.xyz, specular.xyz, 1, true, seed);
	directLight += instanceMaterials[instanceId].selfLight;

	// Add the eye light.
	float specularExponent = instanceMaterials[instanceId].specularExponent;
	float eyeLightLambertFactor = max(dot(normal.xyz, -rayDirection), 0.0f);
	float3 eyeLightReflected = reflect(rayDirection, normal.xyz);
	float3 eyeLightSpecularFactor = specular.rgb * pow(max(saturate(dot(eyeLightReflected, -rayDirection)), 0.0f), specularExponent);
	directLight += (eyeLightDiffuseColor.rgb * eyeLightLambertFactor + eyeLightSpecularColor.rgb * eyeLightSpecularFactor);

	gDirectLight[launchIndex] = float4(directLight, 1.0f);
}