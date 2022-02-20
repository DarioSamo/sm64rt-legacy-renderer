/*
*
*   Volumetric Fog
* 
*/

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

float4 CalculateVolumetrics(float3 rayOrigin, float3 rayDirection, float3 vertexPosition, uint2 launchIndex, uint lightGroupMaskBits)
{
    float4 resColor = float4(0, 0, 0, 0);
	
    // Get the max distance for sampling
    float vertexDistance = min(volumetricDistance, length(vertexPosition - rayOrigin) - RAY_MIN_DISTANCE);
    
    // Grab a weighted average of the sampled light levels
    // Start off at the minimum distance of epsilon
    // End until it reaches the vertex distance
    float sampleDistance = EPSILON;
    float samples = 0.0f;
    float weight = 1.0f;
    float stepDistance = 1.0f / max(EPSILON, volumetricSteps);
    
    // This function has a banding issue which is remedied by randomizing the distance between samples
    float r = getBlueNoise(launchIndex, samples + frameCount).r * (weight - 1.0f);
    while (sampleDistance + r < vertexDistance) {
	    // Get the light level at each sample 
        float3 samplePosition = rayOrigin + rayDirection * (sampleDistance + r);
        resColor.rgb += ComputeLightRandomNoNormal(launchIndex, samplePosition, maxLights, lightGroupMaskBits, true) * weight;
        
        // Exponentially increase the sample distance
        samples += weight;
        sampleDistance *= (1.0f + stepDistance);
        weight *= (1.0f + stepDistance);
        r = getBlueNoise(launchIndex, samples + frameCount).x * (weight - 1.0f); 
    }
    
    resColor.rgb /= max(samples, EPSILON);
    resColor.rgb *= volumetricIntensity;
    resColor.a = RGBtoLuminance(resColor.rgb);
	
    return resColor;
}

[shader("raygeneration")]
void VolumetricFogRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims = DispatchRaysDimensions().xy;
    float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
    float3 nonNormRayDir = d.x * cameraU.xyz + d.y * cameraV.xyz + cameraW.xyz;
    float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
    float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
    float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
    int instanceId = gInstanceId[launchIndex / volumetricResolution];
    if (instanceId < 0) {
        // TODO: Can probably just use a rough estimation according to the fog factors here.
        gVolumetrics[launchIndex] = CalculateVolumetrics(rayOrigin, normalize(rayDirection), rayOrigin + rayDirection * RAY_MAX_DISTANCE, launchIndex, 0x0000FFFF);
        return;
    }

    float3 position = gShadingPosition[launchIndex / volumetricResolution].xyz;
    gVolumetrics[launchIndex] = CalculateVolumetrics(rayOrigin, normalize(rayDirection), position, launchIndex, instanceMaterials[instanceId].lightGroupMaskBits);
}