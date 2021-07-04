//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Lights.hlsli"
#include "Ray.hlsli"
#include "Random.hlsli"
#include "Textures.hlsli"
#include "GlobalParams.hlsli"
#include "SkyPlaneUV.hlsli"

#define RAY_MIN_DISTANCE					0.1f
#define RAY_MAX_DISTANCE					100000.0f
#define MAX_LIGHTS							32
#define FULL_QUALITY_ALPHA					0.999f
#define GI_MINIMUM_ALPHA					0.25f

#define DEBUG_HIT_COUNT						0

#define VISUALIZATION_MODE_NORMAL			0
#define VISUALIZATION_MODE_LIGHTS			1
#define VISUALIZATION_MODE_FLOW				2

// Has better results for avoiding shadow terminator glitches, but has unintended side effects on
// terrain with really bad normals or geometry that had backfaces removed to be optimized and
// therefore can't cast shadows.
#define SKIP_BACKFACE_SHADOWS				0

SamplerState gBackgroundSampler : register(s0);

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

float3 ComputeLightsOrdered(float3 rayDirection, uint instanceId, float3 position, float3 normal, float3 specular, uint maxLights, const bool checkShadows, uint seed) {
	float3 resultLight = float3(0.0f, 0.0f, 0.0f);
	uint lightGroupMaskBits = instanceMaterials[instanceId].lightGroupMaskBits;
	float ignoreNormalFactor = instanceMaterials[instanceId].ignoreNormalFactor;
	if (lightGroupMaskBits > 0) {
		// Build an array of the n closest lights by measuring their intensity.
		uint sMaxLightCount = min(maxLights, MAX_LIGHTS);
		float sLightIntensityFactors[MAX_LIGHTS + 1];
		uint sLightIndices[MAX_LIGHTS + 1];
		uint sLightCount = 0;
		uint gLightCount, gLightStride;
		SceneLights.GetDimensions(gLightCount, gLightStride);
		for (uint l = 0; l < gLightCount; l++) {
			if (lightGroupMaskBits & SceneLights[l].groupBits) {
				float lightIntensityFactor = CalculateLightIntensitySimple(l, position, normal, ignoreNormalFactor);
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

		float3 lightingFactors;
		sLightCount = min(sLightCount, sMaxLightCount);
		for (uint s = 0; s < sLightCount; s++) {
			resultLight += ComputeLight(sLightIndices[s], rayDirection, instanceId, position, normal, specular, checkShadows, seed + s);
		}
	}

	return resultLight;
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

float4 ComputeFog(uint instanceId, float3 position) {
	float4 fogColor = float4(instanceMaterials[instanceId].fogColor, 0.0f);
	float fogMul = instanceMaterials[instanceId].fogMul;
	float fogOffset = instanceMaterials[instanceId].fogOffset;
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
	return gBackground.SampleLevel(gBackgroundSampler, bgPos, 0).rgb;
}

float4 SampleSky2D(float2 screenUV) {
	if (skyPlaneTexIndex >= 0) {
		float2 skyUV = ComputeSkyPlaneUV(screenUV, viewI, viewport.zw);
		float4 skyColor = gTextures[skyPlaneTexIndex].SampleLevel(gBackgroundSampler, skyUV, 0);
		if (any(skyHSLModifier)) {
			skyColor.rgb = ModRGBWithHSL(skyColor.rgb, skyHSLModifier.xyz);
		}

		return skyColor;
	}
	else {
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

float4 SampleSkyPlane(float3 rayDirection) {
	if (skyPlaneTexIndex >= 0) {
		float skyYaw = atan2(rayDirection.x, -rayDirection.z);
		float skyPitch = atan2(-rayDirection.y, sqrt(rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z));
		float2 skyUV = float2((skyYaw + M_PI) / (M_PI * 2.0f), (skyPitch + M_PI) / (M_PI * 2.0f));
		float4 skyColor = gTextures[skyPlaneTexIndex].SampleLevel(gBackgroundSampler, skyUV, 0);
		if (any(skyHSLModifier)) {
			skyColor.rgb = ModRGBWithHSL(skyColor.rgb, skyHSLModifier.xyz);
		}

		return skyColor;
	}
	else {
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

float3 SimpleShadeFromGBuffers(uint hitOffset, uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims, const bool checkShadows, const bool giBounce, uint seed) {
	// Mix background and sky color together.
	float3 bgColor = SampleBackgroundAsEnvMap(rayDirection);
	float4 skyColor = SampleSkyPlane(rayDirection);
	bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

	// Process hits.
	float4 resColor = float4(0, 0, 0, 1);
	float3 simpleLightsResult = float3(0.0f, 0.0f, 0.0f);
	uint maxSimpleLights = 1;
	for (uint hit = hitOffset; hit < hitCount; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = gHitInstanceId[hitBufferIndex];
			uint lightGroupMaskBits = instanceMaterials[instanceId].lightGroupMaskBits;
			float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
			float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float3 specular = instanceMaterials[instanceId].specularColor * vertexSpecular.rgb;
			float3 resultLight = instanceMaterials[instanceId].selfLight;
			float3 resultGiLight = float3(0.0f, 0.0f, 0.0f);

			// Reuse the previous computed lights result if available.
			if (lightGroupMaskBits > 0) {
				if (maxSimpleLights > 0) {
					simpleLightsResult = ComputeLightsOrdered(rayDirection, instanceId, vertexPosition, vertexNormal, specular, 1, checkShadows, seed + hit);
					maxSimpleLights--;
				}

				resultLight += simpleLightsResult;
			}

			resultLight += ambientLightColor.rgb + resultGiLight;
			hitColor.rgb *= resultLight;

			// Backwards alpha blending.
			resColor.rgb += hitColor.rgb * alphaContrib;
			resColor.a *= (1.0 - hitColor.a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	float bgMult = giBounce ? giSkyStrength : 1.0f;
	float resMult = giBounce ? giDiffuseStrength : 1.0f;
	return lerp(bgColor.rgb * bgMult, resColor.rgb * resMult, (1.0 - resColor.a));
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

float3 TraceSimple(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint hitOffset, uint2 launchIndex, uint2 pixelDims, const bool checkShadows, const bool giBounce, uint seed) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist, hitOffset);
	return SimpleShadeFromGBuffers(hitOffset, min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims, checkShadows, giBounce, seed);
}

float FresnelReflectAmount(float3 normal, float3 incident, float reflectivity, float fresnelMultiplier) {
	// TODO: Probably use a more accurate approximation than this.
	float ret = pow(clamp(1.0f + dot(normal, incident), EPSILON, 1.0f), 5.0f);
	return reflectivity + ((1.0 - reflectivity) * ret * fresnelMultiplier);
}

float4 ComputeReflection(float reflectionFactor, float reflectionShineFactor, float reflectionFresnelFactor, float3 rayDirection, float3 position, float3 normal, uint hitOffset, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float3 reflectionDirection = reflect(rayDirection, normal);
	float4 reflectionColor = float4(TraceSimple(position, reflectionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, hitOffset, launchIndex, pixelDims, false, false, seed), 0.0f);
	const float3 HighlightColor = float3(1.0f, 1.05f, 1.2f);
	const float3 ShadowColor = float3(0.1f, 0.05f, 0.0f);
	const float BlendingExponent = 3.0f;
	reflectionColor.rgb = lerp(reflectionColor.rgb, HighlightColor, pow(max(reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.rgb = lerp(reflectionColor.rgb, ShadowColor, pow(max(-reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.a = FresnelReflectAmount(normal, rayDirection, reflectionFactor, reflectionFresnelFactor);
	return reflectionColor;
}

float2 WorldToScreenPos(float4x4 viewProj, float3 worldPos) {
	float4 clipSpace = mul(viewProj, float4(worldPos, 1.0f));
	float3 NDC = clipSpace.xyz / clipSpace.w;
	return (0.5f + NDC.xy / 2.0f);
}

void FullShadeFromGBuffers(uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float2 screenUV = float2(launchIndex.x, launchIndex.y) / float2(pixelDims.x, pixelDims.y);
	float4 skyColor = SampleSky2D(screenUV);
	float4 resColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
	float4 finalAlbedo = skyColor;
	float4 finalNormal = float4(-rayDirection, 0.0f);
	float4 finalFlow = float4(0.0f, 0.0f, 0.0f, 0.0f); // TODO: Motion vector for the sky plane.
	float3 simpleLightsResult = float3(0.0f, 0.0f, 0.0f);
	uint maxRefractions = 1;
	uint maxSimpleLights = 1;
	uint maxFullLights = 1;
	uint maxGI = 1;
	uint maxSimpleLightSamples = min(maxLightSamples, 2);
	for (uint hit = 0; hit < hitCount; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
		uint instanceId = gHitInstanceId[hitBufferIndex];
		float hitDistance = WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
		seed += asuint(hitDistance);

		float4 hitColor = gHitColor[hitBufferIndex];
		float3 vertexPosition = rayOrigin + rayDirection * hitDistance;
		float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
		float refractionFactor = instanceMaterials[instanceId].refractionFactor;
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float3 vertexFlow = gHitDistAndFlow[hitBufferIndex].yzw;
			float2 prevPos = WorldToScreenPos(prevViewProj, vertexPosition - vertexFlow);
			float2 curPos = WorldToScreenPos(viewProj, vertexPosition);
			float2 resultFlow = float2(0.0f, 0.0f);
			if ((prevPos.x >= 0.0f) && (prevPos.x < 1.0f) && (prevPos.y >= 0.0f) && (prevPos.y < 1.0f)) {
				resultFlow = curPos - prevPos;
			}

			uint lightGroupMaskBits = instanceMaterials[instanceId].lightGroupMaskBits;
			float3 resultLight = instanceMaterials[instanceId].selfLight;
			float3 resultGiLight = float3(0.0f, 0.0f, 0.0f);
			if (lightGroupMaskBits > 0) {
				float3 specular = instanceMaterials[instanceId].specularColor * vertexSpecular;
				bool solidColor = (hitColor.a >= FULL_QUALITY_ALPHA);
				bool lastHit = (((hit + 1) >= hitCount) && (refractionFactor <= EPSILON));

				// Full light sampling.
				if ((maxFullLights > 0) && (solidColor || lastHit)) {
					finalAlbedo = hitColor;
					finalNormal = float4(vertexNormal, 0.0f);
					finalFlow = float4(resultFlow * resolution.xy, 0.0f, 0.0f);
					resultLight += ComputeLightsRandom(rayDirection, instanceId, vertexPosition, vertexNormal, specular, maxLightSamples, true, seed);
					maxFullLights--;
				}
				// Simple light sampling. Reuse previous result if calculated once already.
				else {
					if (maxSimpleLights > 0) {
						simpleLightsResult += ComputeLightsRandom(rayDirection, instanceId, vertexPosition, vertexNormal, specular, maxSimpleLightSamples, true, seed);
						maxSimpleLights--;
					}

					resultLight += simpleLightsResult;
				}

				// Global illumination.
				bool alphaGIRequired = (alphaContrib >= GI_MINIMUM_ALPHA);
				if ((maxGI > 0) && (alphaGIRequired || lastHit)) {
					uint giSamples = giBounces;
					uint seedCopy = seed;
					while (giSamples > 0) {
						float3 bounceDir = getCosHemisphereSample(seedCopy, vertexNormal);
						float3 bounceColor = TraceSimple(vertexPosition, bounceDir, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, hitCount, launchIndex, pixelDims, true, true, seed + giSamples);
						resultGiLight += bounceColor / giBounces;
						giSamples--;
					}

					maxGI--;
				}

				// Eye light.
				float specularExponent = instanceMaterials[instanceId].specularExponent;
				float eyeLightLambertFactor = max(dot(vertexNormal, -rayDirection), 0.0f);
				float3 eyeLightReflected = reflect(rayDirection, vertexNormal);
				float3 eyeLightSpecularFactor = specular * pow(max(saturate(dot(eyeLightReflected, -rayDirection)), 0.0f), specularExponent);
				resultLight += (eyeLightDiffuseColor.rgb * eyeLightLambertFactor + eyeLightSpecularColor.rgb * eyeLightSpecularFactor);
			}
			
			resultLight += ambientLightColor.rgb + resultGiLight;
			hitColor.rgb *= resultLight;

			// Add reflections.
			float reflectionFactor = instanceMaterials[instanceId].reflectionFactor;
			if (reflectionFactor > EPSILON) {
				float reflectionFresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
				float reflectionShineFactor = instanceMaterials[instanceId].reflectionShineFactor;
				float4 reflectionColor = ComputeReflection(reflectionFactor, reflectionShineFactor, reflectionFresnelFactor, rayDirection, vertexPosition, vertexNormal, hitCount, launchIndex, pixelDims, seed);
				hitColor.rgb = lerp(hitColor.rgb, reflectionColor.rgb, reflectionColor.a);
			}

			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			if (instanceMaterials[instanceId].fogEnabled) {
				float4 fogColor = ComputeFog(instanceId, vertexPosition);
				hitColor.rgb = lerp(hitColor.rgb, fogColor.rgb, fogColor.a);
			}

			// Special visualization modes for debugging.
			if (visualizationMode == VISUALIZATION_MODE_LIGHTS) {
				hitColor.rgb = resultLight;
			}
			else if (visualizationMode == VISUALIZATION_MODE_FLOW) {
				hitColor.rg = abs(resultFlow.xy);
				hitColor.b = 0.0f;
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

	// Finally, blend with the sky.
	resColor.rgb += skyColor.rgb * (resColor.a * skyColor.a);
	resColor.a *= (1.0f - skyColor.a);

	if (visualizationMode != VISUALIZATION_MODE_NORMAL) {
		finalAlbedo = float4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	gOutput[launchIndex] = float4(resColor.rgb, (1.0f - resColor.a));
	gAlbedo[launchIndex] = finalAlbedo;
	gNormal[launchIndex] = finalNormal;
	gFlow[launchIndex] = finalFlow;

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

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}