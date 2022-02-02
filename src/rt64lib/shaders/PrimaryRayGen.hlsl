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
#include "Textures.hlsli"
#include "BgSky.hlsli"
#include "Lights.hlsli"
#include "Fog.hlsli"

float2 WorldToScreenPos(float4x4 viewProj, float3 worldPos) {
	float4 clipSpace = mul(viewProj, float4(worldPos, 1.0f));
	float3 NDC = clipSpace.xyz / clipSpace.w;
	return (0.5f + NDC.xy / 2.0f);
}

float FresnelReflectAmount(float3 normal, float3 incident, float reflectivity, float fresnelMultiplier) {
	// TODO: Probably use a more accurate approximation than this.
	float ret = pow(max(1.0f + dot(normal, incident), EPSILON), 5.0f);
	return reflectivity + ((1.0 - reflectivity) * ret * fresnelMultiplier);
}

[shader("raygeneration")]
void PrimaryRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims = DispatchRaysDimensions().xy;
    int instanceId = gInstanceId[launchIndex];
	float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
	float3 nonNormRayDir = d.x * cameraU.xyz + d.y * cameraV.xyz + cameraW.xyz;
	float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
	float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
    float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;

	// Initialize the buffers.
	gViewDirection[launchIndex] = float4(rayDirection, 0.0f);
	gReflection[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	gRefraction[launchIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f);

	// Sample the background.
	float2 screenUV = float2(launchIndex.x, launchIndex.y) / float2(launchDims.x, launchDims.y);
    float3 bgPosition = rayOrigin + rayDirection * RAY_MAX_DISTANCE;
	float2 prevBgPos = WorldToScreenPos(prevViewProj, bgPosition);
    float2 curBgPos = WorldToScreenPos(viewProj, bgPosition);
    float3 bgColor = SampleBackground2D(screenUV);
    float4 skyColor = SampleSky2D(screenUV);
    bgColor = lerp(bgColor, skyColor.rgb, skyColor.a);

	// Compute ray differentials.
	RayDiff rayDiff;
	rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
	computeRayDiffs(nonNormRayDir, cameraU.xyz, cameraV.xyz, resolution.zw, rayDiff.dDdx, rayDiff.dDdy);

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

	// Process hits.
	float3 resPosition = float3(0.0f, 0.0f, 0.0f);
	float3 resNormal = -rayDirection;
	float3 resSpecular = float3(0.0f, 0.0f, 0.0f);
	float3 resTransparent = float3(0.0f, 0.0f, 0.0f);
    float3 resTransparentLight = float3(0.0f, 0.0f, 0.0f);
	bool resTransparentLightComputed = false;
	float4 resColor = float4(0, 0, 0, 1);
	float2 resFlow = (curBgPos - prevBgPos) * resolution.xy;
	float resDepth = 1.0f;
	int resInstanceId = -1;
	for (uint hit = 0; hit < payload.nhits; hit++) {
		uint hitBufferIndex = getHitBufferIndex(hit, launchIndex, launchDims);
		float4 hitColor = gHitColor[hitBufferIndex];
		float alphaContrib = (resColor.a * hitColor.a);
		if (alphaContrib >= EPSILON) {
			uint instanceId = gHitInstanceId[hitBufferIndex];
			bool usesLighting = (instanceMaterials[instanceId].lightGroupMaskBits > 0);
			bool applyLighting = usesLighting && (hitColor.a > APPLY_LIGHTS_MINIMUM_ALPHA);
            bool lightShafts = (processingFlags & 0x1) == 1;
            float vertexDistance = WithoutDistanceBias(gHitDistAndFlow[hitBufferIndex].x, instanceId);
            float3 vertexPosition = rayOrigin + rayDirection * vertexDistance;
			float3 vertexNormal = gHitNormal[hitBufferIndex].xyz;
			float3 vertexSpecular = gHitSpecular[hitBufferIndex].rgb;
			float3 specular = instanceMaterials[instanceId].specularColor * vertexSpecular.rgb;
			float reflectionFactor = instanceMaterials[instanceId].reflectionFactor;
			float refractionFactor = instanceMaterials[instanceId].refractionFactor;
			   
			// Calculate the fog for the resulting color using the camera data if the option is enabled.
			bool storeHit = false;
			/*   
			if (instanceMaterials[instanceId].fogEnabled) {
				float4 fogColor = ComputeFogFromCamera(instanceId, vertexPosition);
				resTransparent += fogColor.rgb * fogColor.a * alphaContrib;
				alphaContrib *= (1.0f - fogColor.a);   
			}  
			*/ 
			// Scene-driven fog. Volumetric lighting is on a different shader 
			{ 
                float4 fogColor = SceneFogFromOrigin(vertexPosition, rayOrigin, ambientFogFactors.x, ambientFogFactors.y, ambientFogColor);
                float4 groundFog = SceneGroundFogFromOrigin(vertexPosition, rayOrigin, groundFogFactors.x, groundFogFactors.y, groundFogHeightFactors.x, groundFogHeightFactors.y, groundFogColor);
                float4 combinedColor = float4(0.f, 0.f, 0.f, 0.f);
                combinedColor = BlendAOverB(fogColor, groundFog);
                if (lightShafts) {
                    combinedColor.a *= 1.50f;
                }
                gFog[launchIndex] = combinedColor;
            } 

			// Reflection.
            if (reflectionFactor > EPSILON)
            {
				float reflectionFresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
                float fresnelAmount = FresnelReflectAmount(vertexNormal, rayDirection, reflectionFactor, reflectionFresnelFactor);
                gReflection[launchIndex].rgb = hitColor.rgb;				// For a planned metalness implementation
                gReflection[launchIndex].a = fresnelAmount * alphaContrib;
				alphaContrib *= (1.0f - fresnelAmount);
				storeHit = true;
			}

			// Add the color to the hit color or the transparent buffer if the lighting is disabled.
			float3 resColorAdd = hitColor.rgb * alphaContrib;
			if (applyLighting) {
				storeHit = true;
				resColor.rgb += resColorAdd;
			}
			// Expensive case: transparent geometry that is not solid enough to act as the main
			// instance in the deferred pass and it also needs lighting to work correctly.
			// We sample one light at random and use it for any other transparent geometry that
			// has the same problem.
			else if (usesLighting) {
				if (!resTransparentLightComputed) {
                    resTransparentLight = ComputeLightsRandom(launchIndex, rayDirection, instanceId, vertexPosition, vertexNormal, specular, 1, instanceMaterials[instanceId].lightGroupMaskBits, instanceMaterials[instanceId].ignoreNormalFactor, true);
					resTransparentLightComputed = true;
				}

				resTransparent += resColorAdd * (ambientBaseColor.rgb + ambientNoGIColor.rgb + instanceMaterials[instanceId].selfLight + resTransparentLight);
			}
			// Cheap case: we ignore the geometry entirely from the lighting pass and just add
			// it to the transparency buffer directly.
			else {
				resTransparent += resColorAdd * (ambientBaseColor.rgb + ambientNoGIColor.rgb + instanceMaterials[instanceId].selfLight);
			}

			resColor.a *= (1.0 - hitColor.a);

			// Refraction. Stop searching for more hits afterwards.
			if (refractionFactor > EPSILON) {
				storeHit = true;
				gRefraction[launchIndex].a = resColor.a;
				resColor.a = 0.0f;
			}

			// Store hit if it's been flagged as the primary one.
			if (storeHit && (resInstanceId < 0)) {
				float3 vertexFlow = gHitDistAndFlow[hitBufferIndex].yzw;
				float2 prevPos = WorldToScreenPos(prevViewProj, vertexPosition - vertexFlow);
				float2 curPos = WorldToScreenPos(viewProj, vertexPosition);
				float4 projPos = mul(viewProj, float4(vertexPosition, 1.0f));
				resPosition = vertexPosition;
				resNormal = vertexNormal;
				resSpecular = specular;
				resInstanceId = instanceId;
				resFlow = (curPos - prevPos) * resolution.xy;
				resDepth = projPos.z / projPos.w;
			}
		}

		if (resColor.a <= EPSILON) {
			break;
		}
	}

	// Blend with the background.
    resColor.rgb += bgColor * resColor.a; 
    resColor.a = 1.0f - resColor.a;
	
	/*
    if ((bgColor.x + bgColor.y + bgColor.z) / 3.0f > 1.0f)
    {
        resColor = float4(1.0f, 0.0f, 1.0f, 1.0f);
    }*/

	// Store shading information buffers.
	gShadingPosition[launchIndex] = float4(resPosition, 0.0f);
	gShadingNormal[launchIndex] = float4(resNormal, 0.0f);
	gShadingSpecular[launchIndex] = float4(resSpecular, 0.0f);
	gDiffuse[launchIndex] = resColor;
	gInstanceId[launchIndex] = resInstanceId;
	gTransparent[launchIndex] = float4(resTransparent, 1.0f);
	gFlow[launchIndex] = float2(-resFlow.x, resFlow.y);
	gNormal[launchIndex] = float4(resNormal, 0.0f);
	gDepth[launchIndex] = resDepth;
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}