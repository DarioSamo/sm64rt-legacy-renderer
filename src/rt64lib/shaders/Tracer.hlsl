//
// RT64
//

#include "Camera.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Instances.hlsli"
#include "Lights.hlsli"
#include "Ray.hlsli"
#include "Samplers.hlsli"
#include "ShadowSamples.hlsli"

#define SAMPLE_QUALITY_MIN				8
#define SAMPLE_QUALITY_MAX				32

#define RAY_MIN_DISTANCE				0.2f
#define RAY_MAX_DISTANCE				100000.0f

#define EPSILON							0.000001f

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

float3 ComputeSampleFactors(float3 rayDirection, float3 perpX, float3 perpY, float3 position, float3 normal, float specularIntensity, float specularExponent, float ignoreNormalFactor, float3 lightPosition, float lightRadius, float lightAttenuation, float pointRadius, uint s, bool checkShadow, float maxShadowDist) {
	float3 samplePosition = lightPosition + perpX * ShadowSamples[s].x * pointRadius + perpY * ShadowSamples[s].y * pointRadius;
	float sampleDistance = length(position - samplePosition);
	float3 sampleDirection = normalize(samplePosition - position);
	float sampleIntensityFactor = pow(max(1.0f - (sampleDistance / lightRadius), 0.0f), lightAttenuation);
	float sampleLambertFactor = lerp(max(dot(normal, sampleDirection), 0.0f), 1.0f, ignoreNormalFactor) * sampleIntensityFactor;
	float3 reflectedLight = reflect(-sampleDirection, normal);
	float sampleShadowFactor = ((sampleLambertFactor > 0.0f) && checkShadow) ? TraceShadow(position, sampleDirection, RAY_MIN_DISTANCE, maxShadowDist) : 0.0f;
	return float3(
		sampleLambertFactor,
		specularIntensity * pow(max(saturate(dot(reflectedLight, -rayDirection) * sampleIntensityFactor), 0.0f), specularExponent),
		sampleShadowFactor
	);
}

float3 ComputeLightingFactors(float3 rayDirection, float3 position, float3 normal, float specularIntensity, float specularExponent, float ignoreNormalFactor, uint l, uint sampleLevels) {
	float3 lightingFactors = float3(0.0f, 0.0f, 0.0f);
	float3 lightPosition = SceneLights[l].position;
	float3 lightDelta = position - lightPosition;
	float lightDistance = length(lightDelta);
	float lightRadius = SceneLights[l].attenuationRadius;
	float lightAttenuation = SceneLights[l].attenuationExponent;
	if (lightDistance < lightRadius) {
		float pointRadius = SceneLights[l].pointRadius;
		float3 lightDirection = normalize(lightDelta);
		float3 perpX = cross(-lightDirection, float3(0.f, 1.0f, 0.f));
		if (all(perpX == 0.0f)) {
			perpX.x = 1.0;
		}

		float3 perpY = cross(perpX, -lightDirection);
		float maxShadowDist = max(lightDistance - SceneLights[l].shadowOffset, 0.0f);
		bool checkShadow = SceneLights[l].shadowOffset < lightRadius;
		float shadowFactorVariance = 0.0f;
		for (uint s = 0; s < sampleLevels; s++) {
			float3 sampleFactors = ComputeSampleFactors(rayDirection, perpX, perpY, position, normal, specularIntensity, specularExponent, ignoreNormalFactor, lightPosition, lightRadius, lightAttenuation, pointRadius, s, checkShadow, maxShadowDist);
			shadowFactorVariance += (s > 0) ? abs(sampleFactors.z - (lightingFactors.z / s)) : 0.0f;
			lightingFactors += sampleFactors;
		}

		if (shadowFactorVariance >= EPSILON) {
			sampleLevels = SAMPLE_QUALITY_MAX;
			for (uint s = SAMPLE_QUALITY_MIN; s < sampleLevels; s++) {
				lightingFactors += ComputeSampleFactors(rayDirection, perpX, perpY, position, normal, specularIntensity, specularExponent, ignoreNormalFactor, lightPosition, lightRadius, lightAttenuation, pointRadius, s, checkShadow, maxShadowDist);
			}
		}
	}

	return lightingFactors / sampleLevels;
}

float3 ComputeLights(float3 rayDirection, uint instanceId, float3 position, float3 normal, bool multisamplingEnabled) {
	// Light 0 is always assumed to be the ambient light.
	float ignoreNormalFactor = instanceProps[instanceId].materialProperties.ignoreNormalFactor;
	float specularIntensity = instanceProps[instanceId].materialProperties.specularIntensity;
	float specularExponent = instanceProps[instanceId].materialProperties.specularExponent;
	uint lightGroupMaskBits = instanceProps[instanceId].materialProperties.lightGroupMaskBits;
	float3 selfLight = instanceProps[instanceId].materialProperties.selfLight;
	float3 resultLight = SceneLights[0].diffuseColor + selfLight;
	uint lightCount, lightStride;
	SceneLights.GetDimensions(lightCount, lightStride);
	for (uint l = 1; l < lightCount; l++) {
		if (lightGroupMaskBits & SceneLights[l].groupBits) {
			uint sampleLevels = ((SceneLights[l].pointRadius > 0.0f) && multisamplingEnabled) ? SAMPLE_QUALITY_MIN : 1;
			float3 lightingFactors = ComputeLightingFactors(rayDirection, position, normal, specularIntensity, specularExponent, ignoreNormalFactor, l, sampleLevels);
			resultLight += (SceneLights[l].diffuseColor * lightingFactors.x + SceneLights[l].diffuseColor * SceneLights[l].specularIntensity * lightingFactors.y) * lightingFactors.z;
		}
	}
	
	// Add eye light.
	float3 eyeLightColor = float3(0.15f, 0.15f, 0.15f);
	resultLight += eyeLightColor * max(dot(normal, -rayDirection), 0.0f);
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

float3 SimpleShadeFromGBuffers(uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims) {
	float2 bgPos = float2(rayDirection.z / sqrt(rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z), rayDirection.y);
	float3 bgColor = SampleTexture(gBackground, bgPos, 1, 1, 1).rgb;
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

	for (hit = 0; hit < hitCount; hit++) {
		float alphaContrib = (resColor.a * hitColors[hit].a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = hitInstanceIds[hit];
			float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(hitDistances[hit], instanceId);
			float3 vertexNormal = hitNormals[hit].xyz;
			float3 resultLight = ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, false);
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

uint TraceSurface(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist) {
	// Fill ray.
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	// Fill payload.
	HitInfo payload;
	payload.nhits = 0;

	// Make call.
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, ray, payload);
	return payload.nhits;
}

float3 TraceSimple(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint2 launchIndex, uint2 pixelDims) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist);
	return SimpleShadeFromGBuffers(min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims);
}

float FresnelReflectAmount(float n1, float n2, float3 normal, float3 incident, float reflectivity) {
	// Schlick aproximation
	float r0 = (n1 - n2) / (n1 + n2);
	r0 *= r0;
	float cosX = -dot(normal, incident);
	if (n1 > n2) {
		float n = n1 / n2;
		float sinT2 = n * n * (1.0 - cosX * cosX);
		// Total internal reflection
		if (sinT2 > 1.0)
			return 1.0;

		cosX = sqrt(1.0 - sinT2);
	}

	float x = 1.0 - cosX;
	float ret = r0 + (1.0 - r0) * x * x * x * x * x;

	// adjust reflect multiplier for object reflectivity
	ret = (reflectivity + (1.0 - reflectivity) * ret);
	return ret;
}

float4 ComputeReflection(float reflectionFactor, float reflectionShineFactor, float3 rayDirection, float3 position, float3 normal, uint2 launchIndex, uint2 pixelDims) {
	float3 reflectionDirection = reflect(rayDirection, normal);
	float4 reflectionColor = float4(TraceSimple(position, reflectionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, launchIndex, pixelDims), 0.0f);
	const float3 HighlightColor = float3(1.0f, 1.05f, 1.2f);
	const float3 ShadowColor = float3(0.1f, 0.05f, 0.0f);
	const float BlendingExponent = 3.0f;
	reflectionColor.rgb = lerp(reflectionColor.rgb, HighlightColor, pow(max(reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	reflectionColor.rgb = lerp(reflectionColor.rgb, ShadowColor, pow(max(-reflectionDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	const float N1 = 1.0f;
	const float N2 = 1.33f;
	reflectionColor.a = FresnelReflectAmount(N1, N2, normal, rayDirection, reflectionFactor);
	return reflectionColor;
}

float3 FullShadeFromGBuffers(uint hitCount, float3 rayOrigin, float3 rayDirection, uint2 launchIndex, uint2 pixelDims) {
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

	uint maxRefractions = 1;
	for (hit = 0; hit < hitCount; hit++) {
		uint instanceId = hitInstanceIds[hit];
		float3 vertexPosition = rayOrigin + rayDirection * WithoutDistanceBias(hitDistances[hit], instanceId);
		float3 vertexNormal = hitNormals[hit].xyz;
		float alphaContrib = (resColor.a * hitColors[hit].a);
		if (alphaContrib >= EPSILON) {
			float3 resultLight = ComputeLights(rayDirection, instanceId, vertexPosition, vertexNormal, alphaContrib >= 0.5F);
			hitColors[hit].rgb *= resultLight;

			// Add reflections.
			float reflectionFactor = instanceProps[instanceId].materialProperties.reflectionFactor;
			if (reflectionFactor > 0.0f) {
				float reflectionShineFactor = instanceProps[instanceId].materialProperties.reflectionShineFactor;
				float4 reflectionColor = ComputeReflection(reflectionFactor, reflectionShineFactor, rayDirection, vertexPosition, vertexNormal, launchIndex, pixelDims);
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
			hitCount = TraceSurface(vertexPosition, refractionDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE);
			for (hit = 0; hit < hitCount; hit++) {
				hitBufferIndex = getHitBufferIndex(hit, launchIndex, pixelDims);
				hitDistances[hit] = gHitDistance[hitBufferIndex];
				hitColors[hit] = gHitColor[hitBufferIndex];
				hitNormals[hit] = gHitNormal[hitBufferIndex];
				hitInstanceIds[hit] = gHitInstanceId[hitBufferIndex];
			}

			hit = -1;
			rayOrigin = vertexPosition;
			rayDirection = refractionDirection;
			maxRefractions--;
		}
	}

	// Use last hit color as background color if we hit the maximum amount of queries available.
	if (hitCount == MAX_HIT_QUERIES) {
		bgColor = hitColors[hitCount - 1].rgb;
	}

	return lerp(bgColor.rgb, saturate(resColor.rgb), (1.0 - resColor.a));
}

float3 TraceFull(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist, uint2 launchIndex, uint2 pixelDims) {
	uint hitCount = TraceSurface(rayOrigin, rayDirection, rayMinDist, rayMaxDist);
	return FullShadeFromGBuffers(min(hitCount, MAX_HIT_QUERIES), rayOrigin, rayDirection, launchIndex, pixelDims);
}

[shader("raygeneration")]
void TraceRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	float4 fgColor = gForeground[launchIndex];

	// Don't trace rays on pixels covered by the foreground.
	if (fgColor.a < 1.0f) {
		float2 d = (((launchIndex.xy + 0.5f) / float2(DispatchRaysDimensions().xy)) * 2.f - 1.f);
		float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
		float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
		float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
		float3 fullColor = TraceFull(rayOrigin, rayDirection, RAY_MIN_DISTANCE, RAY_MAX_DISTANCE, launchIndex, DispatchRaysDimensions().xy);
		gOutput[launchIndex] = float4(lerp(fullColor, fgColor.rgb, fgColor.a), 1.0f);
	}
	else {
		gOutput[launchIndex] = fgColor;
	}
}
