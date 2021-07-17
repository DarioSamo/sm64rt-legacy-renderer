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

float FresnelReflectAmount(float3 normal, float3 incident, float reflectivity, float fresnelMultiplier) {
	// TODO: Probably use a more accurate approximation than this.
	float ret = pow(clamp(1.0f + dot(normal, incident), EPSILON, 1.0f), 5.0f);
	return reflectivity + ((1.0 - reflectivity) * ret * fresnelMultiplier);
}

[shader("raygeneration")]
void ReflectionRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;
	int instanceId = gInstanceId[launchIndex];
	float reflectionAlpha = gReflection[launchIndex].a;
	if ((instanceId < 0) || (reflectionAlpha <= EPSILON)) {
		return;
	}

	// Grab the ray origin and direction from the buffers.
	float3 shadingPosition = gShadingPosition[launchIndex].xyz;
	float3 viewDirection = gViewDirection[launchIndex].xyz;
	float3 shadingNormal = gShadingNormal[launchIndex].xyz;
	float3 rayDirection = reflect(viewDirection, shadingNormal);
	float newReflectionAlpha = 0.0f;

	// Mix background and sky color together.
	float3 bgColor = SampleBackgroundAsEnvMap(rayDirection);
	float4 skyColor = SampleSkyPlane(rayDirection);
	bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

	// Trace.
	RayDesc ray;
	ray.Origin = shadingPosition;
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
	int resInstanceId = -1;
	float4 resColor = float4(0, 0, 0, 1);
	for (uint hit = 0; hit < payload.nhits; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint hitInstanceId = gHitInstanceId[hitBufferIndex];
			float3 vertexPosition = shadingPosition + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, hitInstanceId);
			float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float3 specular = instanceMaterials[hitInstanceId].specularColor * vertexSpecular.rgb;
			float reflectionFactor = instanceMaterials[hitInstanceId].reflectionFactor;
			if (reflectionFactor > EPSILON) {
				float reflectionFresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
				float fresnelAmount = FresnelReflectAmount(vertexNormal, rayDirection, reflectionFactor, reflectionFresnelFactor);
				newReflectionAlpha += fresnelAmount * alphaContrib * reflectionAlpha;
			}

			resColor.rgb += hitColor.rgb * alphaContrib;
			resColor.a *= (1.0 - hitColor.a);
			resPosition = vertexPosition;
			resNormal = vertexNormal;
			resSpecular = specular;
			resInstanceId = hitInstanceId;
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	if (resInstanceId >= 0) {
		uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);
		float3 directLight = ComputeLightsRandom(rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, false, seed) + instanceMaterials[resInstanceId].selfLight;
		resColor.rgb *= (ambientBaseColor.rgb + ambientNoGIColor.rgb + directLight);
		gShadingPosition[launchIndex] = float4(resPosition, 0.0f);
		gViewDirection[launchIndex] = float4(rayDirection, 0.0f);
		gShadingNormal[launchIndex] = float4(resNormal, 0.0f);
		gInstanceId[launchIndex] = resInstanceId;
	}

	// Blend with the background.
	resColor.rgb += bgColor * resColor.a;
	resColor.a = 1.0f;

	// Artificial shine factor.
	const float3 HighlightColor = float3(1.0f, 1.05f, 1.2f);
	const float3 ShadowColor = float3(0.1f, 0.05f, 0.0f);
	const float BlendingExponent = 3.0f;
	float reflectionShineFactor = instanceMaterials[instanceId].reflectionShineFactor;
	resColor.rgb = lerp(resColor.rgb, HighlightColor, pow(max(rayDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	resColor.rgb = lerp(resColor.rgb, ShadowColor, pow(max(-rayDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));

	// Add reflection result.
	gReflection[launchIndex].rgb += resColor.rgb * reflectionAlpha * saturate(1.0f - newReflectionAlpha);

	// Store parameters for new reflection.
	gReflection[launchIndex].a = saturate(newReflectionAlpha);
}