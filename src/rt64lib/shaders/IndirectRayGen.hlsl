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

#include "Common.hlsli"

[shader("raygeneration")]
void IndirectRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
	int instanceId = gInstanceId[launchIndex];
	if ((instanceId >= 0) && (giSamples > 0)) {
		uint2 launchDims = DispatchRaysDimensions().xy;
		float3 rayOrigin = gShadingPosition[launchIndex].xyz;
		float3 shadingNormal = gShadingNormal[launchIndex].xyz;
		float3 newIndirect = float3(0.0f, 0.0f, 0.0f);
		float historyLength = 0.0f;

		// Reproject previous indirect.
		if (giReproject) {
			const float WeightNormalExponent = 128.0f;
			float2 flow = gFlow[launchIndex].xy;
			int2 prevIndex = int2(launchIndex + float2(0.5f, 0.5f) + flow);
			float prevDepth = gPrevDepth[prevIndex];
			float3 prevNormal = gPrevNormal[prevIndex].xyz;
			float4 prevIndirectAccum = gPrevIndirectLightAccum[prevIndex];
			float depth = gDepth[launchIndex];
			float weightDepth = abs(depth - prevDepth) / 0.01f;
			float weightNormal = pow(max(0.0f, dot(prevNormal, shadingNormal)), WeightNormalExponent);
			float historyWeight = exp(-weightDepth) * weightNormal;
			newIndirect = prevIndirectAccum.rgb;
			historyLength = prevIndirectAccum.a * historyWeight;
		}

		uint maxSamples = giSamples;
		const uint blueNoiseMult = 64 / giSamples;
		while (maxSamples > 0) {
            float3 rayDirection = microfacetGGX(launchIndex, frameCount + maxSamples * blueNoiseMult, 1.0, shadingNormal);

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
            for (uint hit = 0; hit < payload.nhits; hit++)
            {
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
            float3 resIndirect = ambientBaseColor.rgb;
            if (resInstanceId >= 0) {
                float2x3 lightMatrix = ComputeLightsRandom(launchIndex, rayDirection, resInstanceId, resPosition, resNormal, resSpecular, 1, instanceMaterials[instanceId].lightGroupMaskBits, instanceMaterials[instanceId].ignoreNormalFactor, true);
                float3 directLight = lightMatrix._11_12_13 + instanceMaterials[resInstanceId].selfLight;
                float3 specularLight = lightMatrix._21_22_23 * RGBtoLuminance(directLight);
                if ((processingFlags & 0x8) == 0x8)
                {
                    specularLight *= RGBtoLuminance(directLight);
                    specularLight.r = MetalAmount(specularLight.r, resColor.r, instanceMaterials[resInstanceId].metallicFactor);
                    specularLight.g = MetalAmount(specularLight.g, resColor.g, instanceMaterials[resInstanceId].metallicFactor);
                    specularLight.b = MetalAmount(specularLight.b, resColor.b, instanceMaterials[resInstanceId].metallicFactor);
                }
				
                if ((processingFlags & 0x4) == 0x4) {
                    float3 indirectLight = (resColor.rgb * (1.0f - resColor.a) * directLight + specularLight) * giDiffuseStrength;
                    resIndirect = indirectLight;
                } else {
                    float3 indirectLight = (resColor.rgb * (1.0f - resColor.a) * (ambientBaseColor.rgb + ambientNoGIColor.rgb + directLight) + specularLight) * giDiffuseStrength;
                    resIndirect += indirectLight;
                }
            }
			
            resIndirect += bgColor * giSkyStrength * resColor.a;

			// Accumulate.
            historyLength = min(historyLength + 1.0f, 64.0f);
            newIndirect = lerp(newIndirect.rgb, resIndirect, 1.0f / historyLength);
			
            maxSamples--;
        }
		
		gIndirectLightAccum[launchIndex] = float4(newIndirect, historyLength);
	}
	else {
		gIndirectLightAccum[launchIndex] = float4(ambientBaseColor.rgb + ambientNoGIColor.rgb, 0.0f);
	}
}