//
// RT64
//

#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Random.hlsli"
#include "Ray.hlsli"

[shader("raygeneration")]
void PrimaryRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);

	// Trace.
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = RAY_MIN_DISTANCE;
	ray.TMax = RAY_MAX_DISTANCE;
	HitInfo payload;
	payload.nhits = 0;
	payload.ohits = 0;
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);

	// Process hits.
	float4 resPosition = float4(0, 0, 0, 1);
	float4 resNormal = float4(0, 0, 0, 1);
	float4 resSpecular = float4(0, 0, 0, 1);
	float4 resColor = float4(0, 0, 0, 1);
	int resInstanceId = -1;
	for (uint hit = 0; hit < payload.nhits; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = gHitInstanceId[hitBufferIndex];
			float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
			float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float3 specular = instanceMaterials[instanceId].specularColor * vertexSpecular.rgb;
			resColor.rgb += hitColor.rgb * alphaContrib;
			resColor.a *= (1.0 - hitColor.a);
			resPosition.xyz = vertexPosition;
			resNormal.xyz = vertexNormal;
			resSpecular.xyz = specular;
			resInstanceId = instanceId;
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	gShadingPosition[launchIndex] = resPosition;
	gShadingNormal[launchIndex] = resNormal;
	gShadingSpecular[launchIndex] = resSpecular;
	gDiffuse[launchIndex] = float4(resColor.rgb, 1.0f - resColor.a);
	gInstanceId[launchIndex] = resInstanceId;
}