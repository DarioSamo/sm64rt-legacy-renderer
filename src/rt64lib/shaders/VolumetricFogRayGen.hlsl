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

float4 CalculateVolumetrics(float3 rayOrigin, float3 rayDirection, float3 vertexDistance, uint2 launchIndex, uint lightGroupMaskBits)
{
    float4 resColor = float4(0, 0, 0, 0);
    float3 vertexPosition = rayOrigin + rayDirection * vertexDistance;
	
	// Get the light level at each sample
    for (int shaftSample = 1; shaftSample <= volumetricMaxSamples; shaftSample++)
    {
        float3 samplePosition = rayOrigin + rayDirection * vertexDistance * (float(shaftSample) / float(volumetricMaxSamples));
        resColor.rgb += ComputeLightAtPointRandom(launchIndex, samplePosition, maxLights, lightGroupMaskBits, true);
    }
    resColor.rgb /= volumetricMaxSamples;
    resColor.rgb *= volumetricIntensity;
    resColor.a = RGBtoLuminance(resColor.rgb);
	
    return resColor;
}

[shader("raygeneration")]
void VolumetricFogRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    if ((processingFlags & 0x1) == 0) {
        gVolumetrics[launchIndex] = float4(0, 0, 0, 1);
        return;
    }

    uint2 launchDims = DispatchRaysDimensions().xy;
    float2 d = (((launchIndex.xy + 0.5f + pixelJitter) / float2(launchDims)) * 2.f - 1.f);
    float3 nonNormRayDir = d.x * cameraU.xyz + d.y * cameraV.xyz + cameraW.xyz;
    float4 target = mul(projectionI, float4(d.x, -d.y, 1, 1));
    float3 rayOrigin = mul(viewI, float4(0, 0, 0, 1)).xyz;
    float3 rayDirection = mul(viewI, float4(target.xyz, 0)).xyz;
    int instanceId = gInstanceId[launchIndex * 4];
    if (instanceId < 0) {
        // TODO: Can probably just use a rough estimation according to the fog factors here.
        gVolumetrics[launchIndex] = CalculateVolumetrics(rayOrigin, rayDirection, RAY_MAX_DISTANCE / 100.0, launchIndex, 0x0000FFFF);
        return;
    }

    float3 position = gShadingPosition[launchIndex * 4].xyz;
    float3 rayToPos = position - rayOrigin;
    float vertexDistance = length(rayToPos) - RAY_MIN_DISTANCE;
    rayDirection = normalize(rayToPos);
    gVolumetrics[launchIndex] = CalculateVolumetrics(rayOrigin, rayDirection, vertexDistance, launchIndex, instanceMaterials[instanceId].lightGroupMaskBits & 0x0000FFFF);
}