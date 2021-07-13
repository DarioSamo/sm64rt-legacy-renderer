//
// RT64
//

#include "Color.hlsli"
#include "Constants.hlsli"
#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "Materials.hlsli"
#include "Instances.hlsli"
#include "Random.hlsli"
#include "Ray.hlsli"
#include "SkyPlaneUV.hlsli"
#include "Textures.hlsli"

SamplerState gBackgroundSampler : register(s0);

float2 WorldToScreenPos(float4x4 viewProj, float3 worldPos) {
	float4 clipSpace = mul(viewProj, float4(worldPos, 1.0f));
	float3 NDC = clipSpace.xyz / clipSpace.w;
	return (0.5f + NDC.xy / 2.0f);
}

float3 SampleBackground2D(float2 screenUV) {
	return gBackground.SampleLevel(gBackgroundSampler, screenUV, 0).rgb;
}

float4 SampleSky2D(float2 screenUV) {
	if (skyPlaneTexIndex >= 0) {
		float2 skyUV = ComputeSkyPlaneUV(screenUV, viewI, viewport.zw, skyYawOffset);
		float4 skyColor = gTextures[skyPlaneTexIndex].SampleLevel(gBackgroundSampler, skyUV, 0);
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

[shader("raygeneration")]
void PrimaryRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	uint2 launchDims = DispatchRaysDimensions().xy;
	float2 d = (((launchIndex.xy + 0.5f) / float2(launchDims)) * 2.f - 1.f);
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
	float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
	uint seed = initRand(launchIndex.x + launchIndex.y * launchDims.x, randomSeed, 16);

	// Reset the reflection and refraction buffers.
	gReflection[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	gRefraction[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);

	// Sample the background.
	float2 screenUV = float2(launchIndex.x, launchIndex.y) / float2(launchDims.x, launchDims.y);
	float3 bgColor = SampleBackground2D(screenUV);
	float4 skyColor = SampleSky2D(screenUV);
	float3 bgPosition = rayOrigin + rayDirection * RAY_MAX_DISTANCE;
	float2 prevBgPos = WorldToScreenPos(prevViewProj, bgPosition);
	float2 curBgPos = WorldToScreenPos(viewProj, bgPosition);
	bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

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
	float3 resNormal = -rayDirection;
	float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
	float4 resColor = float4(0, 0, 0, 1);
	float2 resFlow = (curBgPos - prevBgPos) * resolution.xy;
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
			float reflectionFactor = instanceMaterials[instanceId].reflectionFactor;
			float refractionFactor = instanceMaterials[instanceId].refractionFactor;

			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			if (instanceMaterials[instanceId].fogEnabled) {
				float4 fogColor = ComputeFog(instanceId, vertexPosition);
				hitColor.rgb = lerp(hitColor.rgb, fogColor.rgb, fogColor.a);
			}

			resColor.rgb += hitColor.rgb * alphaContrib;
			resColor.a *= (1.0 - hitColor.a);
			
			// Store the primary hit data if the alpha requirment is met or this is the last hit.
			bool primaryHitAlpha = hitColor.a >= PRIMARY_HIT_MINIMUM_ALPHA;
			bool lastHit = ((hit + 1) >= payload.nhits) || (refractionFactor > 0.0f);
			if ((resInstanceId < 0) && (primaryHitAlpha || lastHit)) {
				// Calculate motion vector in screen space.
				float3 vertexFlow = gHitDistAndFlow[hitBufferIndex].yzw;
				float2 prevPos = WorldToScreenPos(prevViewProj, vertexPosition - vertexFlow);
				float2 curPos = WorldToScreenPos(viewProj, vertexPosition);
				resPosition = vertexPosition;
				resNormal = vertexNormal;
				resSpecular = specular;
				resInstanceId = instanceId;
				resFlow = (curPos - prevPos) * resolution.xy;
			}
			
			// Store reflection amount and direction.
			if (reflectionFactor > EPSILON) {
				float3 reflectionDirection = reflect(rayDirection, vertexNormal);
				float reflectionFresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
				gReflection[launchIndex].xyz = reflectionDirection;
				gReflection[launchIndex].a = alphaContrib * FresnelReflectAmount(vertexNormal, rayDirection, reflectionFactor, reflectionFresnelFactor);
			}

			// Store refraction amount and direction.
			if (refractionFactor > EPSILON) {
				float3 refractionDirection = refract(rayDirection, vertexNormal, refractionFactor);
				gRefraction[launchIndex].xyz = refractionDirection;
				gRefraction[launchIndex].a = alphaContrib;
				resColor.a = 0.0f;
			}
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	// Blend with the background.
	resColor.rgb += bgColor * resColor.a;
	resColor.a = 1.0f - resColor.a;

	// Store shading information buffers.
	gShadingPosition[launchIndex] = float4(resPosition, 0.0f);
	gShadingNormal[launchIndex] = float4(resNormal, 0.0f);
	gShadingSpecular[launchIndex] = float4(resSpecular, 0.0f);
	gDiffuse[launchIndex] = resColor;
	gInstanceId[launchIndex] = resInstanceId;
	gFlow[launchIndex] = float4(resFlow, 0.0f, 0.0f);
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}