//
// RT64
//

#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "Lights.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"

#define MAX_LIGHTS 32

float TraceShadow(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist) {
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	ShadowHitInfo shadowPayload;
	shadowPayload.shadowHit = 1.0f;

	uint flags = RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

#if SKIP_BACKFACE_SHADOWS == 1
	flags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
#endif

	TraceRay(SceneBVH, flags, 0xFF, 1, 0, 1, ray, shadowPayload);
	return shadowPayload.shadowHit;
}

float CalculateLightIntensitySimple(uint l, float3 position, float3 normal, float ignoreNormalFactor) {
	float3 lightPosition = SceneLights[l].position;
	float lightRadius = SceneLights[l].attenuationRadius;
	float lightAttenuation = SceneLights[l].attenuationExponent;
	float lightDistance = length(position - lightPosition);
	float3 lightDirection = normalize(lightPosition - position);
	float NdotL = dot(normal, lightDirection);
	const float surfaceBiasDotOffset = 0.707106f;
	float surfaceBias = max(lerp(NdotL, 1.0f, ignoreNormalFactor) + surfaceBiasDotOffset, 0.0f);
	float sampleIntensityFactor = pow(max(1.0f - (lightDistance / lightRadius), 0.0f), lightAttenuation) * surfaceBias;
	return sampleIntensityFactor * dot(SceneLights[l].diffuseColor, float3(1.0f, 1.0f, 1.0f));
}

float3 ComputeLight(uint lightIndex, float3 rayDirection, uint instanceId, float3 position, float3 normal, float3 specular, const bool checkShadows, uint seed) {
	float ignoreNormalFactor = instanceMaterials[instanceId].ignoreNormalFactor;
	float specularExponent = instanceMaterials[instanceId].specularExponent;
	float shadowRayBias = instanceMaterials[instanceId].shadowRayBias;
	float3 lightPosition = SceneLights[lightIndex].position;
	float3 lightDirection = normalize(lightPosition - position);
	float lightRadius = SceneLights[lightIndex].attenuationRadius;
	float lightAttenuation = SceneLights[lightIndex].attenuationExponent;
	float lightPointRadius = (softLightSamples > 0) ? SceneLights[lightIndex].pointRadius : 0.0f;
	float3 perpX = cross(-lightDirection, float3(0.f, 1.0f, 0.f));
	if (all(perpX == 0.0f)) {
		perpX.x = 1.0;
	}

	float3 perpY = cross(perpX, -lightDirection);
	float shadowOffset = SceneLights[lightIndex].shadowOffset;
	const uint maxSamples = max(softLightSamples, 1);
	uint samples = maxSamples;
	float lLambertFactor = 0.0f;
	float3 lSpecularityFactor = 0.0f;
	float lShadowFactor = 0.0f;
	while (samples > 0) {
		float2 sampleCoordinate = float2(nextRand(seed), nextRand(seed)) * 2.0f - 1.0f;
		sampleCoordinate = normalize(sampleCoordinate) * saturate(length(sampleCoordinate));

		float3 samplePosition = lightPosition + perpX * sampleCoordinate.x * lightPointRadius + perpY * sampleCoordinate.y * lightPointRadius;
		float sampleDistance = length(position - samplePosition);
		float3 sampleDirection = normalize(samplePosition - position);
		float sampleIntensityFactor = pow(max(1.0f - (sampleDistance / lightRadius), 0.0f), lightAttenuation);
		float3 reflectedLight = reflect(-sampleDirection, normal);
		float NdotL = max(dot(normal, sampleDirection), 0.0f);
		float sampleLambertFactor = lerp(NdotL, 1.0f, ignoreNormalFactor) * sampleIntensityFactor;
		float sampleShadowFactor = 1.0f;
		if (checkShadows) {
			sampleShadowFactor = TraceShadow(position, sampleDirection, RAY_MIN_DISTANCE + shadowRayBias, (sampleDistance - shadowOffset));
		}

		float3 sampleSpecularityFactor = specular * pow(max(saturate(dot(reflectedLight, -rayDirection) * sampleIntensityFactor), 0.0f), specularExponent);
		lLambertFactor += sampleLambertFactor / maxSamples;
		lSpecularityFactor += sampleSpecularityFactor / maxSamples;
		lShadowFactor += sampleShadowFactor / maxSamples;

		samples--;
	}

	return (SceneLights[lightIndex].diffuseColor * lLambertFactor + SceneLights[lightIndex].specularColor * lSpecularityFactor) * lShadowFactor;
}

float3 ComputeLightsRandom(float3 rayDirection, uint instanceId, float3 position, float3 normal, float3 specular, uint maxLights, const bool checkShadows, uint seed) {
	float3 resultLight = float3(0.0f, 0.0f, 0.0f);
	uint lightGroupMaskBits = instanceMaterials[instanceId].lightGroupMaskBits;
	float ignoreNormalFactor = instanceMaterials[instanceId].ignoreNormalFactor;
	if (lightGroupMaskBits > 0) {
		uint sLightCount = 0;
		uint gLightCount, gLightStride;
		uint sLightIndices[MAX_LIGHTS + 1];
		float sLightIntensities[MAX_LIGHTS + 1];
		float totalLightIntensity = 0.0f;
		SceneLights.GetDimensions(gLightCount, gLightStride);
		for (uint l = 0; (l < gLightCount) && (sLightCount < MAX_LIGHTS); l++) {
			if (lightGroupMaskBits & SceneLights[l].groupBits) {
				float lightIntensity = CalculateLightIntensitySimple(l, position, normal, ignoreNormalFactor);
				if (lightIntensity > EPSILON) {
					sLightIntensities[sLightCount] = lightIntensity;
					sLightIndices[sLightCount] = l;
					totalLightIntensity += lightIntensity;
					sLightCount++;
				}
			}
		}

		float randomRange = totalLightIntensity;
		uint lLightCount = min(sLightCount, maxLights);

		// TODO: Probability is disabled when more than one light is sampled because it's
		// not trivial to calculate the probability of the dependent events without replacement.
		// In any case, it is likely more won't be needed when a temporally stable denoiser is
		// implemented.
		bool useProbability = lLightCount == 1;
		for (uint s = 0; s < lLightCount; s++) {
			float r = nextRand(seed) * randomRange;
			uint chosen = 0;
			float rLightIntensity = sLightIntensities[chosen];
			while ((chosen < (sLightCount - 1)) && (r >= rLightIntensity)) {
				chosen++;
				rLightIntensity += sLightIntensities[chosen];
			}

			// Store and clear the light intensity from the array.
			float cLightIntensity = sLightIntensities[chosen];
			uint cLightIndex = sLightIndices[chosen];
			float invProbability = useProbability ? (randomRange / cLightIntensity) : 1.0f;
			sLightIntensities[chosen] = 0.0f;
			randomRange -= cLightIntensity;

			// Compute and add the light.
			resultLight += ComputeLight(cLightIndex, rayDirection, instanceId, position, normal, specular, checkShadows, seed + s) * invProbability;
		}
	}

	return resultLight;
}

[shader("raygeneration")]
void IndirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if (instanceId < 0) {
		gIndirectLight[launchIndex] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	uint2 launchDims = DispatchRaysDimensions().xy;
	uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);
	float3 rayOrigin = gShadingPosition[launchIndex].xyz;
	float3 shadingNormal = gShadingNormal[launchIndex].xyz;
	float3 rayDirection = getCosHemisphereSample(seed, shadingNormal);

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
	
	if (resInstanceId >= 0) {
		float3 directLight = ComputeLightsRandom(rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, true, seed);
		float3 indirectLight = resColor.rgb * directLight;
		gIndirectLight[launchIndex] = float4(indirectLight, 1.0f);
	}
	else {
		gIndirectLight[launchIndex] = float4(0.0f, 0.0f, 0.0f, 1.0f); // TODO: Sky color.
	}
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}