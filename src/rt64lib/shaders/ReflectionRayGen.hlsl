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
#include "Fog.hlsli"
#include "BgSky.hlsli"

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
	
	// Preliminary roughness implementation
    float roughness = instanceMaterials[instanceId].roughnessFactor;
    float3 random = float3(0.0f, 0.0f, 0.0f);
    if (roughness >= EPSILON) { 
        random = (getBlueNoise(launchIndex, frameCount) - 0.5f) * roughness;
    }
	
	// Grab the ray origin and direction from the buffers.
    float3 shadingPosition = gShadingPosition[launchIndex].xyz;
    float3 viewDirection = gViewDirection[launchIndex].xyz;
    float3 shadingNormal = gShadingNormal[launchIndex].xyz;
    float3 rayDirection = reflect(viewDirection, shadingNormal) + random;
    float newReflectionAlpha = 0.0f;

	// Mix background and sky color together.
    float3 bgColor = SampleBackgroundAsEnvMap(rayDirection);
    float4 skyColor = SampleSkyPlane(rayDirection );
	bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

	// Ray differential.
	RayDiff rayDiff;
	rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dDdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dDdy = float3(0.0f, 0.0f, 0.0f);

	// Trace.
	RayDesc ray;
    ray.Origin = shadingPosition;
	ray.Direction = rayDirection;
	ray.TMin = RAY_MIN_DISTANCE;
	ray.TMax = RAY_MAX_DISTANCE;
	HitInfo payload;
	payload.nhits = 0;
	payload.rayDiff = rayDiff;
	TraceRay(SceneBVH, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 0, 0, 0, ray, payload);

	// Process hits.
	float3 resPosition = float3(0.0f, 0.0f, 0.0f);
	float3 resNormal = float3(0.0f, 0.0f, 0.0f);
	float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
	int resInstanceId = -1;
	float4 resColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
	float3 resTransparent = float3(0.0f, 0.0f, 0.0f);
    for (uint hit = 0; hit < payload.nhits; hit++)
    {
        uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint hitInstanceId = gHitInstanceId[hitBufferIndex];
			bool usesLighting = (instanceMaterials[hitInstanceId].lightGroupMaskBits > 0);
			float3 vertexPosition = shadingPosition + rayDirection * WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, hitInstanceId);

			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			/*
			if (instanceMaterials[hitInstanceId].fogEnabled) {
				float4 fogColor = ComputeFogFromOrigin(hitInstanceId, vertexPosition, shadingPosition);
				resTransparent += fogColor.rgb * fogColor.a * alphaContrib;
				alphaContrib *= (1.0f - fogColor.a);
            }
			*/
			// Preliminary implementation of scene-driven fog instead of material-driven
			{ 
                float4 fogColor = SceneFogFromOrigin(vertexPosition, shadingPosition, ambientFogFactors.x, ambientFogFactors.y, ambientFogColor);
                float4 groundFog = SceneGroundFogFromOrigin(vertexPosition, shadingPosition, groundFogFactors.x, groundFogFactors.y, groundFogHeightFactors.x, groundFogHeightFactors.y, groundFogColor);
                float4 combinedColor = float4(0.f, 0.f, 0.f, 0.f);
                combinedColor = BlendAOverB(fogColor, groundFog);
                resTransparent += combinedColor.rgb * combinedColor.a * alphaContrib;
                alphaContrib *= (1.0f - combinedColor.a);
            }

			float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float3 specular = instanceMaterials[hitInstanceId].specularColor * vertexSpecular.rgb;
			float reflectionFactor = instanceMaterials[hitInstanceId].reflectionFactor;
			if (reflectionFactor > EPSILON) {
				float reflectionFresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
				float fresnelAmount = FresnelReflectAmount(vertexNormal, rayDirection, reflectionFactor, reflectionFresnelFactor);
				newReflectionAlpha += fresnelAmount * alphaContrib * reflectionAlpha;
			}

			if (usesLighting) {
				resColor.rgb += hitColor.rgb * alphaContrib;
			}
			else {
				resTransparent += hitColor.rgb * alphaContrib * (ambientBaseColor.rgb + ambientNoGIColor.rgb + instanceMaterials[hitInstanceId].selfLight);
			}

			resPosition = vertexPosition;
			resNormal = vertexNormal;
			resSpecular = specular;
			resInstanceId = hitInstanceId;
			resColor.a *= (1.0 - hitColor.a);
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	if (resInstanceId >= 0) {
        float3 directLight = ComputeLightsRandom(launchIndex, rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, instanceMaterials[instanceId].lightGroupMaskBits, instanceMaterials[instanceId].ignoreNormalFactor, false) + instanceMaterials[resInstanceId].selfLight;
        resColor.rgb *= (gIndirectLightAccum[launchIndex].rgb + directLight);
		gShadingPosition[launchIndex] = float4(resPosition, 0.0f);
		gViewDirection[launchIndex] = float4(rayDirection, 0.0f);
		gShadingNormal[launchIndex] = float4(resNormal, 0.0f);
		gInstanceId[launchIndex] = resInstanceId;

    }

	// Blend with the background.
	resColor.rgb += bgColor * resColor.a + resTransparent;
	resColor.a = 1.0f;

	// Artificial shine factor.
	const float3 HighlightColor = float3(1.0f, 1.05f, 1.2f);
	const float3 ShadowColor = float3(0.1f, 0.05f, 0.0f);
	const float BlendingExponent = 3.0f;
	float reflectionShineFactor = instanceMaterials[instanceId].reflectionShineFactor;
	resColor.rgb = lerp(resColor.rgb, HighlightColor, pow(max(rayDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));
	resColor.rgb = lerp(resColor.rgb, ShadowColor, pow(max(-rayDirection.y, 0.0f) * reflectionShineFactor, BlendingExponent));

	// Add reflection result.
	gReflection[launchIndex].rgb += resColor.rgb * reflectionAlpha * max(1.0f - newReflectionAlpha, 0.0);

	// Store parameters for new reflection.
    gReflection[launchIndex].a = newReflectionAlpha;
}