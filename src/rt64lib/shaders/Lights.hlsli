//
// RT64
//

#include "BlueNoise.hlsli"

// Structures

struct LightInfo {
	float3 position;
	float3 diffuseColor;
	float attenuationRadius;
	float pointRadius;
	float3 specularColor;
	float shadowOffset;
	float attenuationExponent;
	float flickerIntensity;
	uint groupBits;
};

// Root signature

StructuredBuffer<LightInfo> SceneLights : register(t4);

#define MAX_LIGHTS 16

float TraceShadow(float3 rayOrigin, float3 rayDirection, float rayMinDist, float rayMaxDist) {
	RayDesc ray;
	ray.Origin = rayOrigin;
	ray.Direction = rayDirection;
	ray.TMin = rayMinDist;
	ray.TMax = rayMaxDist;

	RayDiff rayDiff;
	rayDiff.dOdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dOdy = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dDdx = float3(0.0f, 0.0f, 0.0f);
	rayDiff.dDdy = float3(0.0f, 0.0f, 0.0f);

	ShadowHitInfo shadowPayload;
	shadowPayload.shadowHit = 1.0f;
	shadowPayload.rayDiff = rayDiff;

	uint flags = RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

#if SKIP_BACKFACE_SHADOWS == 1
	flags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
#endif

	TraceRay(SceneBVH, flags, 0xFF, 1, 0, 1, ray, shadowPayload);
	return shadowPayload.shadowHit;
}

float FresnelSpecularAmount(float3 V, float3 H, float specularity, float fresnelMultiplier)
{	
	// TODO: Probably use a more accurate approximation than this.
    float ret = pow(max(1.0f - max(dot(V, H), 0.0), EPSILON), 5.0f);
    return specularity + ((1.0 - specularity) * ret * fresnelMultiplier);
}

float GeometryShadowingGGX(float3 N, float3 X, float roughness)
{
    float k = roughness / 2;
    float NdotX = max(dot(N, X), 0.0);
    return NdotX / max(NdotX * (1.0 - k) + k, EPSILON);
}

float NormalDistributionGGX(float roughness, float3 N, float3 H)
{	
    float rough2 = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (rough2 - 1.0) + 1.0;
    return rough2 / max(M_PI * denom * denom, EPSILON);
    }

float2 CalculateSpecularity(float3 normal, float3 vertexPosition, float3 viewPosition, float3 lightPosition, float roughness, float specularExponent)
{
    float3 N = normalize(normal);
    float3 V = normalize(viewPosition);
    float3 L = normalize(lightPosition - viewPosition);
    float3 H = normalize(V + L);
    float sampleSpecularityFactor = NormalDistributionGGX(roughness, N, H);
    sampleSpecularityFactor *= GeometryShadowingGGX(N, V, roughness) * GeometryShadowingGGX(N, L, roughness);
    float ks = FresnelSpecularAmount(V, H, specularExponent, 1.0);
    sampleSpecularityFactor /= max(4 * max(dot(V, N), 0.0) * max(dot(L, N), 0.0), EPSILON);
    return float2(sampleSpecularityFactor, ks);
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

float2x4 ComputeLight(uint2 launchIndex, uint lightIndex, float3 rayDirection, uint instanceId, float3 position, float3 normal, float3 specular, const bool checkShadows) {
	float ignoreNormalFactor = instanceMaterials[instanceId].ignoreNormalFactor;
	float specularExponent = instanceMaterials[instanceId].specularExponent;
	float shadowRayBias = instanceMaterials[instanceId].shadowRayBias;
    float fresnelFactor = instanceMaterials[instanceId].reflectionFresnelFactor;
	float3 lightPosition = SceneLights[lightIndex].position;
	float3 lightDirection = normalize(lightPosition - position);
	float lightRadius = SceneLights[lightIndex].attenuationRadius;
	float lightAttenuation = SceneLights[lightIndex].attenuationExponent;
	float lightPointRadius = (diSamples > 0) ? SceneLights[lightIndex].pointRadius : 0.0f;
    float roughness = gHitRoughness[instanceId];
	float3 perpX = cross(-lightDirection, float3(0.f, 1.0f, 0.f));
	if (all(perpX == 0.0f)) {
		perpX.x = 1.0;
	}

	float3 perpY = cross(perpX, -lightDirection);
	float shadowOffset = SceneLights[lightIndex].shadowOffset;
	const uint maxSamples = max(diSamples, 1);
	uint samples = maxSamples;
	float lLambertFactor = 0.0f;
	float lSpecularityFactor = 0.0f;
	float lShadowFactor = 0.0f;
    float lFresnelFactor = 0.f;
	while (samples > 0) {
		float2 sampleCoordinate = getBlueNoise(launchIndex, frameCount + samples).rg * 2.0f - 1.0f;
		sampleCoordinate = normalize(sampleCoordinate) * saturate(length(sampleCoordinate));

		float3 samplePosition = lightPosition + perpX * sampleCoordinate.x * lightPointRadius + perpY * sampleCoordinate.y * lightPointRadius;
		float sampleDistance = length(position - samplePosition);
		float3 sampleDirection = normalize(samplePosition - position);
		float sampleIntensityFactor = pow(max(1.0f - (sampleDistance / lightRadius), 0.0f), lightAttenuation);
		float3 reflectedLight = reflect(-sampleDirection, normal);
		float NdotL = max(dot(normal, sampleDirection), 0.0f);
        float sampleLambertFactor = lerp(NdotL, 1.0f, ignoreNormalFactor) * sampleIntensityFactor ;
        float sampleSpecularityFactor = pow(max(saturate(dot(reflectedLight, -rayDirection) * sampleIntensityFactor), 0.0f), specularExponent);
        float sampleShadowFactor = 1.0f;
		if (checkShadows) {
			sampleShadowFactor = TraceShadow(position, sampleDirection, RAY_MIN_DISTANCE + shadowRayBias, (sampleDistance - shadowOffset));
		}
		
		// Cook-Torrence Specular function
        float2 spec = CalculateSpecularity(normal, position, mul(viewI, float4(0, 0, 0, 1)).xyz, samplePosition, roughness, 0.0);
        lLambertFactor += sampleLambertFactor / maxSamples;
        lSpecularityFactor += sampleSpecularityFactor / maxSamples;
		lShadowFactor += sampleShadowFactor / maxSamples;
        lFresnelFactor += spec.y / maxSamples;
		
		samples--;
	}
	
    float2x4 resColor =
    {
        float4(SceneLights[lightIndex].diffuseColor * lLambertFactor * lShadowFactor, 0.0),
		float4(SceneLights[lightIndex].specularColor * specular * lSpecularityFactor * lShadowFactor, lFresnelFactor)
    };
    return resColor ;
}

float2x4 ComputeLightsRandom(uint2 launchIndex, float3 rayDirection, uint instanceId, float3 position, float3 normal, float3 specular, uint maxLightCount, uint lightGroupMaskBits, float ignoreNormalFactor, const bool checkShadows) {
    float2x4 resultLight = 
	{	0.0, 0.0, 0.0, 0.0,
		0.0, 0.0, 0.0, 0.0	};
	
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
		uint lLightCount = min(sLightCount, maxLightCount);

		// TODO: Probability is disabled when more than one light is sampled because it's
		// not trivial to calculate the probability of the dependent events without replacement.
		// In any case, it is likely more won't be needed when a temporally stable denoiser is
		// implemented.
		bool useProbability = lLightCount == 1;
		for (uint s = 0; s < lLightCount; s++) {
			float r = getBlueNoise(launchIndex, frameCount + s).r * randomRange;
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
			resultLight += ComputeLight(launchIndex, cLightIndex, rayDirection, instanceId, position, normal, specular, checkShadows) * invProbability;
		}
	}

	return resultLight;
}

float3 ComputeLightNoNormal(uint2 launchIndex, uint lightIndex, float3 position, const bool checkShadows)
{
    float3 lightPosition = SceneLights[lightIndex].position;
    float3 lightDirection = normalize(lightPosition - position);
    float lightRadius = SceneLights[lightIndex].attenuationRadius;
    float lightAttenuation = SceneLights[lightIndex].attenuationExponent;
    float lightPointRadius = (diSamples > 0) ? SceneLights[lightIndex].pointRadius : 0.0f;
    float3 perpX = cross(-lightDirection, float3(0.f, 1.0f, 0.f));
    if (all(perpX == 0.0f))
    {
        perpX.x = 1.0;
    }

    float3 perpY = cross(perpX, -lightDirection);
    float shadowOffset = SceneLights[lightIndex].shadowOffset;
    const uint maxSamples = max(diSamples, 1);
    uint samples = maxSamples;
    float lLambertFactor = 0.0f;
    float lShadowFactor = 0.0f;
    while (samples > 0)
    {
        float2 sampleCoordinate = getBlueNoise(launchIndex, frameCount + samples).rg * 2.0f - 1.0f;
        sampleCoordinate = normalize(sampleCoordinate) * saturate(length(sampleCoordinate));

        float3 samplePosition = lightPosition + perpX * sampleCoordinate.x * lightPointRadius + perpY * sampleCoordinate.y * lightPointRadius;
        float sampleDistance = length(position - samplePosition);
        float3 sampleDirection = normalize(samplePosition - position);
        float sampleIntensityFactor = pow(max(1.0f - (sampleDistance / lightRadius), 0.0f), lightAttenuation);
        float sampleShadowFactor = 1.0f;
        if (checkShadows) {
            sampleShadowFactor = TraceShadow(position, sampleDirection, RAY_MIN_DISTANCE, (sampleDistance - shadowOffset));
        }
		
        lLambertFactor += sampleIntensityFactor / maxSamples;
        lShadowFactor += sampleShadowFactor / maxSamples;

        samples--;
    }

    return (SceneLights[lightIndex].diffuseColor * lLambertFactor) * lShadowFactor;
}

float CalculateLightIntensityNoNormal(uint l, float3 position)
{
    float3 lightPosition = SceneLights[l].position;
    float lightRadius = SceneLights[l].attenuationRadius;
    float lightAttenuation = SceneLights[l].attenuationExponent;
    float lightDistance = length(position - lightPosition);
    float3 lightDirection = normalize(lightPosition - position);
    float sampleIntensityFactor = pow(max(1.0f - (lightDistance / lightRadius), 0.0f), lightAttenuation);
    return sampleIntensityFactor * dot(SceneLights[l].diffuseColor, float3(1.0f, 1.0f, 1.0f));
}

float3 ComputeLightAtPointRandom(uint2 launchIndex, float3 position, uint maxLightCount, uint lightGroupMaskBits, const bool checkShadows)
{
    float3 resultLight = float3(0.0f, 0.0f, 0.0f);
    if (lightGroupMaskBits > 0)
    {
        uint sLightCount = 0;
        uint gLightCount, gLightStride;
        uint sLightIndices[MAX_LIGHTS + 1];
        float sLightIntensities[MAX_LIGHTS + 1];
        float totalLightIntensity = 0.0f;
        SceneLights.GetDimensions(gLightCount, gLightStride);
        for (uint l = 0; (l < gLightCount) && (sLightCount < MAX_LIGHTS); l++)
        {
            if (lightGroupMaskBits & SceneLights[l].groupBits)
            {
                float lightIntensity = CalculateLightIntensityNoNormal(l, position);
                if (lightIntensity > EPSILON)
                {
                    sLightIntensities[sLightCount] = lightIntensity;
                    sLightIndices[sLightCount] = l;
                    totalLightIntensity += lightIntensity;
                    sLightCount++;
                }
            }
        }

        float randomRange = totalLightIntensity;
        uint lLightCount = min(sLightCount, maxLightCount);

		// TODO: Probability is disabled when more than one light is sampled because it's
		// not trivial to calculate the probability of the dependent events without replacement.
		// In any case, it is likely more won't be needed when a temporally stable denoiser is
		// implemented.
        bool useProbability = lLightCount == 1;
        for (uint s = 0; s < lLightCount; s++)
        {
            float r = getBlueNoise(launchIndex, launchIndex.x + frameCount + s).r * randomRange;
            uint chosen = 0;
            float rLightIntensity = sLightIntensities[chosen];
            while ((chosen < (sLightCount - 1)) && (r >= rLightIntensity))
            {
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
            resultLight += ComputeLightNoNormal(launchIndex, cLightIndex, position, checkShadows) * invProbability;
        }
    }

    return resultLight;
}