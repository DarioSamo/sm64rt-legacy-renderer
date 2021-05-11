//
// RT64
//

#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Instances.hlsli"
#include "Lights.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Samplers.hlsli"
#include "ViewParams.hlsli"

#define EPSILON								1e-6
#define M_PI								3.14159265f
#define RAY_MIN_DISTANCE					1.0f
#define RAY_MAX_DISTANCE					100000.0f
#define MAX_LIGHTS							16
#define FULL_QUALITY_ALPHA					0.999f
#define GI_MINIMUM_ALPHA					0.25f

#define DEBUG_HIT_COUNT						0

// Has better results for avoiding shadow terminator glitches, but has unintended side effects on
// terrain with really bad normals or geometry that had backfaces removed to be optimized and
// therefore can't cast shadows.
//#define SKIP_BACKFACE_SHADOWS

float TraceShadow(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist) {
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	ShadowHitInfo shadowPayload;
	shadowPayload.shadowHit = 1.0f;

	uint flags = RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

#ifdef SKIP_BACKFACE_SHADOWS
	flags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
#endif

	TraceRay(SceneBVH, flags, 0xFF, 1, 0, 1, ray, shadowPayload);
	return shadowPayload.shadowHit;
}

float CalculateLightIntensitySimple(uint l, float3 position) {
	float3 lightPosition = SceneLights[l].position;
	float lightRadius = SceneLights[l].attenuationRadius;
	float lightAttenuation = SceneLights[l].attenuationExponent;
	float lightDistance = length(position - lightPosition);
	float3 lightDirection = normalize(lightPosition - position);
	float sampleIntensityFactor = pow(max(1.0f - (lightDistance / lightRadius), 0.0f), lightAttenuation);
	return sampleIntensityFactor * dot(SceneLights[l].diffuseColor, float3(1.0f, 1.0f, 1.0f));
}

float3 ComputeLights(float3 rayDirection, uint instanceId, float3 position, float3 normal, uint maxLights, const bool checkShadows, uint seed) {
	float3 resultLight = float3(0.0f, 0.0f, 0.0f);
	uint lightGroupMaskBits = instanceProps[instanceId].materialProperties.lightGroupMaskBits;
	if (lightGroupMaskBits > 0) {
		// Build an array of the n closest lights by measuring their intensity.
		uint sMaxLightCount = min(maxLights, MAX_LIGHTS);
		float sLightIntensityFactors[MAX_LIGHTS + 1];
		uint sLightIndices[MAX_LIGHTS + 1];
		uint sLightCount = 0;
		uint gLightCount, gLightStride;
		SceneLights.GetDimensions(gLightCount, gLightStride);
		for (uint l = 1; l < gLightCount; l++) {
			if (lightGroupMaskBits & SceneLights[l].groupBits) {
				float lightIntensityFactor = CalculateLightIntensitySimple(l, position);
				if (lightIntensityFactor > EPSILON) {
					uint hi = min(sLightCount, sMaxLightCount);
					uint lo = hi - 1;
					while ((hi > 0) && (lightIntensityFactor > sLightIntensityFactors[lo])) {
						sLightIntensityFactors[hi] = sLightIntensityFactors[lo];
						sLightIndices[hi] = sLightIndices[lo];
						hi--;
						lo--;
					}

					sLightIntensityFactors[hi] = lightIntensityFactor;
					sLightIndices[hi] = l;
					sLightCount++;
				}
			}
		}

		float ignoreNormalFactor = instanceProps[instanceId].materialProperties.ignoreNormalFactor;
		float specularIntensity = instanceProps[instanceId].materialProperties.specularIntensity;
		float specularExponent = instanceProps[instanceId].materialProperties.specularExponent;
		float shadowRayBias = instanceProps[instanceId].materialProperties.shadowRayBias;
		float3 lightingFactors;
		sLightCount = min(sLightCount, sMaxLightCount);
		for (uint s = 0; s < sLightCount; s++) {
			uint l = sLightIndices[s];

			float3 lightPosition = SceneLights[l].position;
			float3 lightDirection = normalize(lightPosition - position);
			float lightRadius = SceneLights[l].attenuationRadius;
			float lightAttenuation = SceneLights[l].attenuationExponent;
			float lightPointRadius = (softLightSamples > 0) ? SceneLights[l].pointRadius : 0.0f;
			float3 perpX = cross(-lightDirection, float3(0.f, 1.0f, 0.f));
			if (all(perpX == 0.0f)) {
				perpX.x = 1.0;
			}

			float3 perpY = cross(perpX, -lightDirection);
			float shadowOffset = SceneLights[l].shadowOffset;
			const uint maxSamples = max(softLightSamples, 1);
			uint samples = maxSamples;
			float lLambertFactor = 0.0f;
			float lSpecularityFactor = 0.0f;
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

				float sampleSpecularityFactor = specularIntensity * pow(max(saturate(dot(reflectedLight, -rayDirection) * sampleIntensityFactor), 0.0f), specularExponent);
				lLambertFactor += sampleLambertFactor / maxSamples;
				lSpecularityFactor += sampleSpecularityFactor / maxSamples;
				lShadowFactor += sampleShadowFactor / maxSamples;

				samples--;
			}

			resultLight += (SceneLights[l].diffuseColor * lLambertFactor + SceneLights[l].diffuseColor * SceneLights[l].specularIntensity * lSpecularityFactor) * lShadowFactor;
		}
	}

	return resultLight;
}

float4 ComputeFog(uint instanceId, float3 position) {
	float4 fogColor = float4(instanceProps[instanceId].materialProperties.fogColor, 0.0f);
	float fogMul = instanceProps[instanceId].materialProperties.fogMul;
	float fogOffset = instanceProps[instanceId].materialProperties.fogOffset;
	float4 clipPos = mul(mul(projection, view), float4(position.xyz, 1.0f));

	// Values from the game are designed around -1 to 1 space.
	clipPos.z = clipPos.z * 2.0f - clipPos.w;

	float winv = 1.0f / max(clipPos.w, 0.001f);
	const float DivisionFactor = 255.0f;
	fogColor.a = min(max((clipPos.z * winv * fogMul + fogOffset) / DivisionFactor, 0.0f), 1.0f);
	return fogColor;
}


float3 SampleBackgroundAsEnvMap(float3 rayDirection) {
	float2 bgPos = float2(rayDirection.z / sqrt(rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z), rayDirection.y);
	return SampleTexture(gBackground, bgPos, 1, 1, 1).rgb;
}

float3 MixAmbientAndGI(float3 ambientLight, float3 resultGiLight) {
	float lumAmb = dot(ambientLight, float3(1.0f, 1.0f, 1.0f));
	float lumGI = dot(resultGiLight, float3(1.0f, 1.0f, 1.0f));
	
	// Assign intensity based on weight configuration.
	lumAmb = lumAmb * (1.0f - ambGIMixWeight);
	lumGI = lumGI * ambGIMixWeight;

	float invSum = 1.0f / max(lumAmb + lumGI, EPSILON);
	return ambientLight * lumAmb * invSum + resultGiLight * lumGI * invSum;
}

float3 SimpleShadeFromGBuffers(uint hitOffset, uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims, const bool checkShadows, uint seed) {
	float3 bgColor = SampleBackgroundAsEnvMap(rayDirection);
	float4 resColor = float4(0, 0, 0, 1);
	float3 simpleLightsResult = float3(0.0f, 0.0f, 0.0f);
	uint maxSimpleLights = 1;
	for (uint hit = hitOffset; hit < hitCount; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = gHitInstanceId[hitBufferIndex];
			uint lightGroupMaskBits = instanceProps[instanceId].materialProperties.lightGroupMaskBits;
			float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(gHitDistance[hitBufferIndex], instanceId);
			float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 resultLight = instanceProps[instanceId].materialProperties.selfLight;
			float3 resultGiLight = float3(0.0f, 0.0f, 0.0f);

			// Reuse the previous computed lights result if available.
			if (lightGroupMaskBits > 0) {
				if (maxSimpleLights > 0) {
					simpleLightsResult = ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, 1, checkShadows, seed + hit);
					maxSimpleLights--;
				}
				
				// Do fake GI bounces by sampling the background as an environment map.
				uint giSamples = giEnvBounces;
				uint seedCopy = seed;
				while (giSamples > 0) {
					float3 bounceDir = getCosHemisphereSample(seedCopy, vertexNormal);
					float bounceStrength = min(1.0f + bounceDir.y, 1.0f);
					float3 bounceColor = SampleBackgroundAsEnvMap(bounceDir) * bounceStrength;
					resultGiLight += bounceColor / giEnvBounces;
					giSamples--;
				}

				resultLight += simpleLightsResult;
			}

			resultLight += MixAmbientAndGI(SceneLights[0].diffuseColor, resultGiLight);
			hitColor.rgb *= resultLight;

			// Backwards alpha blending.
			resColor.rgb += hitColor.rgb * alphaContrib;
			resColor.a *= (1.0 - hitColor.a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	return lerp(bgColor.rgb, saturate(resColor.rgb), (1.0 - resColor.a));
}

uint TraceSurface(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint rayHitOffset) {
	// Fill ray.
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	// Fill payload.
	HitInfo payload;
	payload.nhits = rayHitOffset;
	payload.ohits = rayHitOffset;

	// Make call.
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);
	return payload.nhits;
}

float3 TraceSimple(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint hitOffset, uint2 launchIndex, uint2 pixelDims, const bool checkShadows, uint seed) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist, hitOffset);
	return SimpleShadeFromGBuffers(hitOffset, min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims, checkShadows, seed);
}

float FresnelReflectAmount(float3 normal, float3 incident, float reflectivity, float fresnelMultiplier) {
	// TODO: Probably use a more accurate approximation than this.
	float ret = pow(clamp(1.0f + dot(normal, incident), EPSILON, 1.0f), 5.0f);
	return reflectivity + ((1.0 - reflectivity) * ret * fresnelMultiplier);
}

float4 ComputeReflection(float reflectionFactor, float reflectionShineFactor, float reflectionFresnelFactor, float3 rayDirection, float3 position, float3 normal, uint hitOffset, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float3 reflectionDirection = reflect(rayDirection, normal);
	float4 reflectionColor = float4(TraceSimple(position, reflectionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, hitOffset, launchIndex, pixelDims, false, seed), 0.0f);
	const float3 HighlightColor = float3(1.0f, 1.05f, 1.2f);
	const float3 ShadowColor = float3(0.1f, 0.05f, 0.0f);
	const float BlendingExponent = 3.0f;
	reflectionColor.rgb = lerp(reflectionColor.rgb, HighlightColor, pow(max(reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.rgb = lerp(reflectionColor.rgb, ShadowColor, pow(max(-reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.a = FresnelReflectAmount(normal, rayDirection, reflectionFactor, reflectionFresnelFactor);
	return reflectionColor;
}

void FullShadeFromGBuffers(uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float4 resColor = float4(0, 0, 0, 1);
	float4 finalAlbedo = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 finalNormal = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float3 simpleLightsResult = float3(0.0f, 0.0f, 0.0f);
	uint maxRefractions = 1;
	uint maxSimpleLights = 1;
	uint maxFullLights = 1;
	uint maxGI = 1;
	for (uint hit = 0; hit < hitCount; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
		uint instanceId = gHitInstanceId[hitBufferIndex];
		float hitDistance = WithoutDistanceBias(gHitDistance[hitBufferIndex], instanceId);
		seed += asuint(hitDistance);

		float4 hitColor = gHitColor[hitBufferIndex];
		float3 vertexPosition = rayOrigin + rayDirection * hitDistance;
		float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
		float refractionFactor = instanceProps[instanceId].materialProperties.refractionFactor;
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint lightGroupMaskBits = instanceProps[instanceId].materialProperties.lightGroupMaskBits;
			float3 resultLight = instanceProps[instanceId].materialProperties.selfLight;
			float3 resultGiLight = float3(0.0f, 0.0f, 0.0f);
			if (lightGroupMaskBits > 0) {
				// Full light sampling.
				bool solidColor = (hitColor.a >= FULL_QUALITY_ALPHA);
				bool lastHit = (((hit + 1) >= hitCount) && (refractionFactor <= EPSILON));
				if ((maxFullLights > 0) && (solidColor || lastHit)) {
					finalAlbedo = hitColor;
					finalNormal = float4(vertexNormal, 0.0f);
					resultLight += ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, maxLightSamples, true, seed);
					maxFullLights--;
				}
				else {
					// Simple light sampling. Reuse previous result if calculated once already.
					if (maxSimpleLights > 0) {
						simpleLightsResult += ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, 2, true, seed);
						maxSimpleLights--;
					}

					resultLight = simpleLightsResult;
				}

				// Global illumination.
				bool alphaGIRequired = (alphaContrib >= GI_MINIMUM_ALPHA);
				if ((maxGI > 0) && (alphaGIRequired || lastHit)) {
					uint giSamples = giBounces;
					uint seedCopy = seed;
					while (giSamples > 0) {
						float3 bounceDir = getCosHemisphereSample(seedCopy, vertexNormal);
						float3 bounceColor = TraceSimple(vertexPosition, bounceDir, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, hitCount, launchIndex, pixelDims, true, seed + giSamples);
						resultGiLight += bounceColor / giBounces;
						giSamples--;
					}

					maxGI--;
				}

				// Eye light.
				float specularIntensity = instanceProps[instanceId].materialProperties.specularIntensity;
				float specularExponent = instanceProps[instanceId].materialProperties.specularExponent;
				float eyeLightLambertFactor = max(dot(vertexNormal, -rayDirection), 0.0f);
				float3 eyeLightReflected = reflect(rayDirection, vertexNormal);
				float eyeLightSpecularFactor = specularIntensity * pow(max(saturate(dot(eyeLightReflected, -rayDirection)), 0.0f), specularExponent);

				// TODO: Make these modifiable.
				float3 eyeLightDiffuseColor = float3(0.15f, 0.15f, 0.15f);
				float3 eyeLightSpecularColor = float3(0.05f, 0.05f, 0.05f);
				resultLight += (eyeLightDiffuseColor * eyeLightLambertFactor + eyeLightSpecularColor * eyeLightSpecularFactor);
			}
			
			resultLight += MixAmbientAndGI(SceneLights[0].diffuseColor, resultGiLight);
			hitColor.rgb *= resultLight;

			// Add reflections.
			float reflectionFactor = instanceProps[instanceId].materialProperties.reflectionFactor;
			if (reflectionFactor > EPSILON) {
				float reflectionFresnelFactor = instanceProps[instanceId].materialProperties.reflectionFresnelFactor;
				float reflectionShineFactor = instanceProps[instanceId].materialProperties.reflectionShineFactor;
				float4 reflectionColor = ComputeReflection(reflectionFactor, reflectionShineFactor, reflectionFresnelFactor, rayDirection, vertexPosition, vertexNormal, hitCount, launchIndex, pixelDims, seed);
				hitColor.rgb = lerp(hitColor.rgb, reflectionColor.rgb, reflectionColor.a);
			}

			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			if (instanceProps[instanceId].ccFeatures.opt_fog) {
				float4 fogColor = ComputeFog(instanceId, vertexPosition);
				hitColor.rgb = lerp(hitColor.rgb, fogColor.rgb, fogColor.a);
			}

			// Backwards alpha blending.
			resColor.rgb += hitColor.rgb * alphaContrib;
			resColor.a *= (1.0f - hitColor.a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}

		// Do refractions.
		if ((refractionFactor > EPSILON) && (maxRefractions > 0)) {
			float3 refractionDirection = refract(rayDirection, vertexNormal, refractionFactor);

			// Perform another trace and fill the rest of the buffers.
			hitCount = TraceSurface(vertexPosition, refractionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, hit + 1);
			rayOrigin = vertexPosition;
			rayDirection = refractionDirection;
			maxRefractions--;
		}
	}

	gOutput[launchIndex] = float4(resColor.rgb, (1.0f - resColor.a));
	gAlbedo[launchIndex] = finalAlbedo;
	gNormal[launchIndex] = finalNormal;

#if DEBUG_HIT_COUNT == 1
	float4 colors[MAX_HIT_QUERIES + 1] =
	{
		float4(0.00, 0.00, 0.00, 1.00),
		float4(0.33, 0.00, 0.00, 1.00),
		float4(0.66, 0.00, 0.00, 1.00),
		float4(1.00, 0.00, 0.00, 1.00),
		float4(0.00, 0.33, 0.00, 1.00),
		float4(0.00, 0.66, 0.00, 1.00),
		float4(0.00, 1.00, 0.00, 1.00),
		float4(0.00, 0.00, 0.33, 1.00),
		float4(0.00, 0.00, 0.66, 1.00),
		float4(0.00, 0.00, 1.00, 1.00),
		float4(0.33, 0.33, 0.00, 1.00),
		float4(0.66, 0.66, 0.00, 1.00),
		float4(1.00, 1.00, 0.00, 1.00),
		float4(0.00, 0.33, 0.33, 1.00),
		float4(0.00, 0.66, 0.66, 1.00),
		float4(0.00, 1.00, 1.00, 1.00),
		float4(1.00, 1.00, 1.00, 1.00),
	};

	gOutput[launchIndex] = colors[hitCount];
#endif
}

void TraceFull(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint2 launchIndex, uint2 pixelDims, uint seed) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist, 0);
	FullShadeFromGBuffers(min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims, seed);
}

[shader("raygeneration")]
void TraceRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f) / float2(launchDims)) * 2.f - 1.f);
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);
	TraceFull(rayOrigin, rayDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, launchIndex, launchDims, seed);
}
