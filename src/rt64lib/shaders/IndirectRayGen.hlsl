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
#include "BgSky.hlsli"

[shader("raygeneration")]
void IndirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if ((instanceId >= 0) && (giBounces > 0)) {
		uint2 launchDims = DispatchRaysDimensions().xy;
		uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);
		float3 rayOrigin = gShadingPosition[launchIndex].xyz;
		float3 shadingNormal = gShadingNormal[launchIndex].xyz;
		float3 rayDirection = getCosHemisphereSample(seed, shadingNormal);
		float3 indirectResult = float3(0.0f, 0.0f, 0.0f);
		uint maxBounces = giBounces;
		while (maxBounces > 0) {
			// Ray differential.
			RayDiff rayDiff;
			rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
			rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
			rayDiff.dDdx = float3(0.0f, 0.0f, 0.0f);
			rayDiff.dDdy = float3(0.0f, 0.0f, 0.0f);

			// Trace.
			RayDesc ray;
			ray.Origin = rayOrigin;
			ray.Direction = rayDirection;
			ray.TMin = RAY_MIN_DISTANCE;
			ray.TMax = RAY_MAX_DISTANCE;
			HitInfo payload;
			payload.nhits = 0;
			payload.rayDiff = rayDiff;
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
					resPosition = vertexPosition;
					resNormal = vertexNormal;
					resSpecular = specular;
					resInstanceId = instanceId;
				}

				if (resColor.a <= EPSILON) {
					break;
				}
			}

			// Add diffuse bounce as indirect light.
			if (resInstanceId >= 0) {
				float3 directLight = ComputeLightsRandom(rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, true, seed) + instanceMaterials[resInstanceId].selfLight;
				float3 indirectLight = resColor.rgb * (1.0f - resColor.a) * (ambientBaseColor.rgb + ambientNoGIColor.rgb + directLight) * giDiffuseStrength;
				indirectResult += indirectLight;
			}

			// Add sky as indirect light.
			indirectResult += bgColor * giSkyStrength * resColor.a;

			maxBounces--;
		}

		gIndirectLight[launchIndex] = float4(ambientBaseColor.rgb + indirectResult / giBounces, 1.0f);
	}
	else {
		gIndirectLight[launchIndex] = float4(ambientBaseColor.rgb + ambientNoGIColor.rgb, 1.0f);
	}
}