//
// RT64
//

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

#define MAX_LIGHTS 32

SamplerState gBackgroundSampler : register(s0);

float2 FakeEnvMapUV(float3 rayDirection, float yawOffset) {
	float yaw = fmod(yawOffset + atan2(rayDirection.x, -rayDirection.z) + M_PI, M_TWO_PI);
	float pitch = fmod(atan2(-rayDirection.y, sqrt(rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z)) + M_PI, M_TWO_PI);
	return float2(yaw / M_TWO_PI, pitch / M_TWO_PI);
}

float3 SampleBackgroundAsEnvMap(float3 rayDirection) {
	return gBackground.SampleLevel(gBackgroundSampler, FakeEnvMapUV(rayDirection, 0.0f), 0).rgb;
}

float4 SampleSkyPlane(float3 rayDirection) {
	if (skyPlaneTexIndex >= 0) {
		float4 skyColor = gTextures[skyPlaneTexIndex].SampleLevel(gBackgroundSampler, FakeEnvMapUV(rayDirection, skyYawOffset), 0);
		skyColor.rgb *= skyDiffuseMultiplier.rgb;

		if (any(skyHSLModifier)) {
			skyColor.rgb = ModRGBWithHSL(skyColor.rgb, skyHSLModifier.rgb);
		}

		return skyColor;
	}
	else {
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

[shader("raygeneration")]
void IndirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if (instanceId < 0) {
		gIndirectLight[launchIndex] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	if (giBounces == 0) {
		gIndirectLight[launchIndex] = float4(ambientBaseColor.rgb + ambientNoGIColor.rgb, 1.0f);
		return;
	}

	uint2 launchDims = DispatchRaysDimensions().xy;
	uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);
	float3 rayOrigin = gShadingPosition[launchIndex].xyz;
	float3 shadingNormal = gShadingNormal[launchIndex].xyz;
	float3 rayDirection = getCosHemisphereSample(seed, shadingNormal);
	float3 indirectResult = float3(0.0f, 0.0f, 0.0f);
	uint maxBounces = giBounces;
	while (maxBounces > 0) {
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

		// Mix background and sky color together.
		float3 bgColor = SampleBackgroundAsEnvMap(rayDirection);
		float4 skyColor = SampleSkyPlane(rayDirection);
		bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

		// Process hits.
		float3 resPosition = float3(0.0f, 0.0f, 0.0f);
		float3 resNormal = float3(0.0f, 0.0f, 0.0f);
		float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
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

				// Store the primary hit data if the alpha requirment is met or this is the last hit.
				bool primaryHitAlpha = hitColor.a >= PRIMARY_HIT_MINIMUM_ALPHA;
				bool lastHit = (hit + 1) >= payload.nhits;
				if ((resInstanceId < 0) && (primaryHitAlpha || lastHit)) {
					resPosition = vertexPosition;
					resNormal = vertexNormal;
					resSpecular = specular;
					resInstanceId = instanceId;
				}
			}

			if (resColor.a <= EPSILON) {
				break;
			}
		}

		// Add diffuse bounce as indirect light.
		if (resInstanceId >= 0) {
			float3 directLight = ComputeLightsRandom(rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, true, seed);
			float3 indirectLight = resColor.rgb * (1.0f - resColor.a) * (ambientBaseColor.rgb + ambientNoGIColor.rgb + directLight) * giDiffuseStrength;
			indirectResult += indirectLight;
		}

		// Add sky as indirect light.
		indirectResult += bgColor * giSkyStrength * resColor.a;

		maxBounces--;
	}

	gIndirectLight[launchIndex] = float4(ambientBaseColor.rgb + indirectResult / giBounces, 1.0f);
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}