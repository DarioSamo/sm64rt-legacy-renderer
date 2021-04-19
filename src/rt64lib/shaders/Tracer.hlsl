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
#include "BlueNoise.hlsli"
#include "ViewParams.hlsli"

#define EPSILON								0.000001f
#define M_PI								3.14159265f
#define RAY_MIN_DISTANCE					0.2f
#define RAY_MAX_DISTANCE					100000.0f
#define MAX_LIGHTS							16
#define FULL_QUALITY_ALPHA					0.999f

float TraceShadow(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist) {
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	ShadowHitInfo shadowPayload;
	shadowPayload.shadowHit = 1.0f;
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 1, 0, 1, ray, shadowPayload);
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

float3 ComputeLights(float3 rayDirection, uint instanceId, float3 position, float3 normal, uint maxLights, uint seed) {
	float3 resultLight = instanceProps[instanceId].materialProperties.selfLight;
	uint lightGroupMaskBits = instanceProps[instanceId].materialProperties.lightGroupMaskBits;

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
			float sampleLambertFactor = lerp(max(dot(normal, sampleDirection), 0.0f), 1.0f, ignoreNormalFactor) * sampleIntensityFactor;
			float sampleShadowFactor = TraceShadow(position, sampleDirection, RAY_MIN_DISTANCE, (sampleDistance - shadowOffset));
			float sampleSpecularityFactor = specularIntensity * pow(max(saturate(dot(reflectedLight, -rayDirection) * sampleIntensityFactor), 0.0f), specularExponent);
			lLambertFactor += sampleLambertFactor / maxSamples;
			lSpecularityFactor += sampleSpecularityFactor / maxSamples;
			lShadowFactor += sampleShadowFactor / maxSamples;

			samples--;
		}

		resultLight += (SceneLights[l].diffuseColor * lLambertFactor + SceneLights[l].diffuseColor * SceneLights[l].specularIntensity * lSpecularityFactor) * lShadowFactor;
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

float3 SimpleShadeFromGBuffers(uint hitOffset, uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float2 bgPos = float2(rayDirection.z / sqrt(rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z), rayDirection.y);
	float3 bgColor = SampleTexture(gBackground, bgPos, 1, 1, 1).rgb;
	float4 resColor = float4(0, 0, 0, 1);
	float hitDistances[MAX_HIT_QUERIES];
	float4 hitColors[MAX_HIT_QUERIES];
	float4 hitNormals[MAX_HIT_QUERIES];
	uint hitInstanceIds[MAX_HIT_QUERIES];
	int hit;
	uint hitBufferIndex;
	for (hit = hitOffset; hit < hitCount; hit++) {
		hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
		hitDistances[hit] = gHitDistance[hitBufferIndex];
		hitColors[hit] = gHitColor[hitBufferIndex];
		hitNormals[hit] = gHitNormal[hitBufferIndex];
		hitInstanceIds[hit] = gHitInstanceId[hitBufferIndex];
	}

	for (hit = hitOffset; hit < hitCount; hit++) {
		float alphaContrib = (resColor.a * hitColors[hit].a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = hitInstanceIds[hit];
			float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(hitDistances[hit], instanceId);
			float3 vertexNormal = hitNormals[hit].xyz;
			float3 resultLight = ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, 1, seed + hit);
			resultLight += SceneLights[0].diffuseColor;
			hitColors[hit].rgb *= resultLight;

			// Backwards alpha blending.
			resColor.rgb += hitColors[hit].rgb * alphaContrib;
			resColor.a *= (1.0 - hitColors[hit].a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	// Use last hit color as background color if we hit the maximum amount of queries available.
	if (hitCount == MAX_HIT_QUERIES) {
		bgColor = hitColors[hitCount - 1].rgb;
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
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);
	return payload.nhits;
}

float3 TraceSimple(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint2 launchIndex, uint2 pixelDims, uint seed) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist, 1);
	return SimpleShadeFromGBuffers(1, min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims, seed);
}

float FresnelReflectAmount(float3 normal, float3 incident, float reflectivity, float fresnelMultiplier) {
	// TODO: Probably use a more accurate approximation than this.
	float ret = pow(clamp(1.0f + dot(normal, incident), EPSILON, 1.0f), 5.0f);
	return reflectivity + ((1.0 - reflectivity) * ret * fresnelMultiplier);
}

float4 ComputeReflection(float reflectionFactor, float reflectionShineFactor, float reflectionFresnelFactor, float3 rayDirection, float3 position, float3 normal, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float3 reflectionDirection = reflect(rayDirection, normal);
	float4 reflectionColor = float4(TraceSimple(position, reflectionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, launchIndex, pixelDims, seed), 0.0f);
	const float3 HighlightColor = float3(1.0f, 1.05f, 1.2f);
	const float3 ShadowColor = float3(0.1f, 0.05f, 0.0f);
	const float BlendingExponent = 3.0f;
	reflectionColor.rgb = lerp(reflectionColor.rgb, HighlightColor, pow(max(reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.rgb = lerp(reflectionColor.rgb, ShadowColor, pow(max(-reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.a = FresnelReflectAmount(normal, rayDirection, reflectionFactor, reflectionFresnelFactor);
	return reflectionColor;
}

float3 FullShadeFromGBuffers(uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims, uint seed) {
	float3 bgColor = gBackground[launchIndex].rgb;
	float4 resColor = float4(0, 0, 0, 1);
	float hitDistances[MAX_HIT_QUERIES];
	float4 hitColors[MAX_HIT_QUERIES];
	float4 hitNormals[MAX_HIT_QUERIES];
	uint hitInstanceIds[MAX_HIT_QUERIES];
	int hit;
	uint hitBufferIndex;
	for (hit = 0; hit < hitCount; hit++) {
		hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
		hitDistances[hit] = gHitDistance[hitBufferIndex];
		hitColors[hit] = gHitColor[hitBufferIndex];
		hitNormals[hit] = gHitNormal[hitBufferIndex];
		hitInstanceIds[hit] = gHitInstanceId[hitBufferIndex];
	}
	
	float4 finalAlbedo = float4(bgColor, 1.0f);
	float4 finalNormal = float4(0.0f, 0.0f, 0.0f, 0.0f);
	uint maxRefractions = 1;
	uint maxFullQuality = 1;
	for (hit = 0; hit < hitCount; hit++) {
		uint instanceId = hitInstanceIds[hit];
		float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(hitDistances[hit], instanceId);
		float3 vertexNormal = hitNormals[hit].xyz;
		float hitColorA = hitColors[hit].a;
		float alphaContrib = (resColor.a * hitColorA);
		if (alphaContrib >= EPSILON) {
			float3 resultLight = float3(0.0f, 0.0f, 0.0f);
			float3 resultGiLight = float3(0.0f, 0.0f, 0.0f);
			if ((hitColorA >= FULL_QUALITY_ALPHA) && (maxFullQuality > 0)) {
				finalAlbedo = hitColors[hit];
				finalNormal = float4(vertexNormal, 0.0f);
				resultLight += ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, maxLightSamples, seed + hit);
				maxFullQuality--;

				// Global illumination.
				uint giSamples = giBounces;
				while (giSamples > 0) {
					float3 bounceDir = getCosHemisphereSample(seed, vertexNormal);
					float3 bounceColor = TraceSimple(vertexPosition, bounceDir, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, launchIndex, pixelDims, seed + giSamples);
					resultGiLight += bounceColor / giBounces;
					giSamples--;
				}
			}
			else {
				resultLight += ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, 2, seed + hit);
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

			// Mix ambient light and GI.
			float3 ambientLight = SceneLights[0].diffuseColor;
			float lumAmb = dot(ambientLight, float3(1.0f, 1.0f, 1.0f));
			float lumGI = dot(resultGiLight, float3(1.0f, 1.0f, 1.0f));

			// Assign intensity based on weight configuration.
			lumAmb = lumAmb * (1.0f - ambGIMixWeight);
			lumGI = lumGI * ambGIMixWeight;

			float invSum = 1.0f / (lumAmb + lumGI);
			resultLight += ambientLight * lumAmb * invSum + resultGiLight * lumGI * invSum;

			hitColors[hit].rgb *= resultLight;

			// Add reflections.
			float reflectionFactor = instanceProps[instanceId].materialProperties.reflectionFactor;
			if (reflectionFactor > 0.0f) {
				float reflectionFresnelFactor = instanceProps[instanceId].materialProperties.reflectionFresnelFactor;
				float reflectionShineFactor = instanceProps[instanceId].materialProperties.reflectionShineFactor;
				float4 reflectionColor = ComputeReflection(reflectionFactor, reflectionShineFactor, reflectionFresnelFactor, rayDirection, vertexPosition, vertexNormal, launchIndex, pixelDims, seed + hit);
				hitColors[hit].rgb = lerp(hitColors[hit].rgb, reflectionColor.rgb, reflectionColor.a);
			}

			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			if (instanceProps[instanceId].ccFeatures.opt_fog) {
				float4 fogColor = ComputeFog(instanceId, vertexPosition);
				hitColors[hit].rgb = lerp(hitColors[hit].rgb, fogColor.rgb, fogColor.a);
			}

			// Backwards alpha blending.
			resColor.rgb += hitColors[hit].rgb * alphaContrib;
			resColor.a *= (1.0 - hitColors[hit].a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}

		// Add refractions
		float refractionFactor = instanceProps[instanceId].materialProperties.refractionFactor;
		if ((refractionFactor > 0.0) && (maxRefractions > 0)) {
			float3 refractionDirection = refract(rayDirection, vertexNormal, refractionFactor);

			// Perform another trace and fill the buffers again. Restart the for loop.
			uint hitOffset = hit + 1;
			hitCount = TraceSurface(vertexPosition, refractionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, hitOffset);
			for (hit = hitOffset; hit < hitCount; hit++) {
				hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
				hitDistances[hit] = gHitDistance[hitBufferIndex];
				hitColors[hit] = gHitColor[hitBufferIndex];
				hitNormals[hit] = gHitNormal[hitBufferIndex];
				hitInstanceIds[hit] = gHitInstanceId[hitBufferIndex];
			}

			hit = hitOffset - 1;
			rayOrigin = vertexPosition;
			rayDirection = refractionDirection;
			maxRefractions--;
		}
	}

	// Use last hit color as background color if we hit the maximum amount of queries available.
	if (hitCount == MAX_HIT_QUERIES) {
		bgColor = hitColors[hitCount - 1].rgb;
	}

	float3 finalColor = lerp(bgColor.rgb, saturate(resColor.rgb), (1.0 - resColor.a));
	int gIndex = launchIndex.y * pixelDims.x + launchIndex.x;
	gColor[gIndex] = float4(finalColor, 1.0f);
	gAlbedo[gIndex] = finalAlbedo;
	gNormal[gIndex] = finalNormal;
	return finalColor;
}

float3 TraceFull(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint2 launchIndex, uint2 pixelDims, uint seed) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist, 0);
	return FullShadeFromGBuffers(min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims, seed);
}

[shader("raygeneration")]
void TraceRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;

	// Don't trace rays on pixels covered by the foreground.
	float4 fgColor = gForeground[launchIndex];
	//if (fgColor.a < 1.0f) {
		float2 d = (((launchIndex.xy + 0.5f) / float2(launchDims)) * 2.f - 1.f);
		float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
		float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
		float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
		uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, /*frameCount*/(int)(rayOrigin.x + rayOrigin.y + rayOrigin.z), 16);
		float3 fullColor = TraceFull(rayOrigin, rayDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, launchIndex, launchDims, seed);
		gOutput[launchIndex] = float4(lerp(fullColor, fgColor.rgb, fgColor.a), 1.0f);
	/*
	}
	else {
		gOutput[launchIndex] = fgColor;
	}
	*/
}
