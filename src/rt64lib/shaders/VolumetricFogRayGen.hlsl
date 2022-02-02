/*
*
*   Volumetric Fog
* 
*/

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Textures.hlsli"
#include "Lights.hlsli"
#include "Fog.hlsli"
#include "BgSky.hlsli"

[shader("raygeneration")]
void VolumetricFogRayGen()
{
	uint2 launchIndex = DispatchRaysIndex().xy;
    if ((processingFlags & 0x1)  == 0) {
        gVolumetrics[launchIndex] = float4(0, 0, 0, 1);;
		return;
	}
	
	uint2 launchDims = DispatchRaysDimensions().xy;
	int instanceId = gInstanceId[launchIndex * 4];
	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float3 nonNormRayDir = d.x * cameraU.xyz + d.y * cameraV.xyz + cameraW.xyz;
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	
	// Compute ray differentials.
	RayDiff rayDiff;
	rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
	computeRayDiffs(nonNormRayDir, cameraU.xyz, cameraV.xyz, resolution.zw, rayDiff.dDdx, rayDiff.dDdy);

	// Trace.
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = RAY_MIN_DISTANCE;
	ray.TMax = RAY_MAX_DISTANCE;
	HitInfo payload;
	payload.nhits = 0;
	payload.rayDiff = rayDiff;

	// End trace upon hit
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);
	
	float4 resColor = float4(0, 0, 0, 0);
	// Just go straight into calculating the volumetrics, since there's only gonna be 1 hit
	if (payload.nhits > 0)
	{
		uint hitBufferIndex = getHitBufferIndex(0, launchIndex, launchDims);
		float vertexDistance = WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
		float3 vertexPosition = rayOrigin + rayDirection * vertexDistance;
	
		// Get the light level at each sample
		for (int shaftSample = 1; shaftSample <= volumetricMaxSamples; shaftSample++)
		{
			float3 samplePosition = rayOrigin + rayDirection * vertexDistance * (float(shaftSample) / float(volumetricMaxSamples));
			resColor.rgb += ComputeLightAtPointRandom(launchIndex, samplePosition, maxLights, instanceMaterials[instanceId].lightGroupMaskBits, true); // Groups 8-16 aren't in the volumetrics
		}
		resColor.rgb /= volumetricMaxSamples;
		resColor.rgb *= volumetricIntensity;
		resColor.a = RGBtoLuminance(resColor.rgb);
	
        gVolumetrics[launchIndex] = resColor;
    }
}