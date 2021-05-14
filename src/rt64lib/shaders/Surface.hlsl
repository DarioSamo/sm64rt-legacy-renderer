//
// RT64
//

#include "GlobalHitBuffers.hlsli"
#include "Instances.hlsli"
#include "Mesh.hlsli"
#include "Ray.hlsli"
#include "Samplers.hlsli"
#include "Textures.hlsli"
#include "ViewParams.hlsli"

[shader("anyhit")]
void SurfaceAnyHit(inout HitInfo payload, Attributes attrib) {
	// Sample texture color and execute color combiner.
	uint instanceId = NonUniformResourceIndex(InstanceIndex());
	uint triangleId = PrimitiveIndex();
	int diffuseTexIndex = instanceProps[instanceId].materialProperties.diffuseTexIndex;
	float4 diffuseColorMix = instanceProps[instanceId].materialProperties.diffuseColorMix;
	float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);
	VertexAttributes vertex = GetVertexAttributes(vertexBuffer, indexBuffer, triangleId, barycentrics);
	float4 texelColor = SampleTexture(gTextures[diffuseTexIndex], vertex.uv, instanceProps[instanceId].materialProperties.filterMode, instanceProps[instanceId].materialProperties.hAddressMode, instanceProps[instanceId].materialProperties.vAddressMode);
	half specularColor = half(1.0h);

	// Only mix the texture if the alpha value is negative.
	texelColor.rgb = lerp(texelColor.rgb, diffuseColorMix.rgb, max(-diffuseColorMix.a, 0.0f));

	ColorCombinerInputs ccInputs;
	ccInputs.input1 = vertex.input[0];
	ccInputs.input2 = vertex.input[1];
	ccInputs.input3 = vertex.input[2];
	ccInputs.input4 = vertex.input[3];
	ccInputs.texVal0 = texelColor;
	ccInputs.texVal1 = texelColor;
	
	uint noiseScale = resolution.y / NOISE_SCALE_HEIGHT;
	uint seed = initRand((DispatchRaysIndex().x / noiseScale) + (DispatchRaysIndex().y / noiseScale) * DispatchRaysDimensions().x, frameCount, 16);
	float4 resultColor = CombineColors(instanceProps[instanceId].ccFeatures, ccInputs, seed);
	resultColor.a = clamp(instanceProps[instanceId].materialProperties.solidAlphaMultiplier * resultColor.a, 0.0f, 1.0f);

	// Ignore hit if alpha is empty.
	static const float Epsilon = 0.00001f;
	if (resultColor.a > Epsilon) {
		// Get best index to store the data on.
		uint2 pixelIdx = DispatchRaysIndex().xy;
		uint2 pixelDims = DispatchRaysDimensions().xy;
		uint hitStride = pixelDims.x * pixelDims.y;

		// HACK: Add some bias for the comparison based on the instance ID so coplanar surfaces are friendlier with each other.
		// This can likely be implemented as an instance property at some point to control depth sorting.
		float tval = WithDistanceBias(RayTCurrent(), instanceId);
		uint hi = getHitBufferIndex(min(payload.nhits, MAX_HIT_QUERIES), pixelIdx, pixelDims);
		uint minHi = getHitBufferIndex(payload.ohits, pixelIdx, pixelDims);
		uint lo = hi - hitStride;
		while ((hi > minHi) && (tval < gHitDistance[lo]))
		{
			gHitDistance[hi] = gHitDistance[lo];
			gHitColor[hi] = gHitColor[lo];
			gHitNormal[hi] = gHitNormal[lo];
			gHitSpecular[hi] = gHitSpecular[lo];
			gHitInstanceId[hi] = gHitInstanceId[lo];
			hi -= hitStride;
			lo -= hitStride;
		}
		
		uint hitPos = hi / hitStride;
		if (hitPos < MAX_HIT_QUERIES) {
			// Only mix the final diffuse color if the alpha is positive.
			resultColor.rgb = lerp(resultColor.rgb, diffuseColorMix.rgb, max(diffuseColorMix.a, 0.0f));

			int normalTexIndex = instanceProps[instanceId].materialProperties.normalTexIndex;
			if (normalTexIndex >= 0) {
				float uvDetailScale = instanceProps[instanceId].materialProperties.uvDetailScale;
				float3 normalColor = SampleTexture(gTextures[normalTexIndex], vertex.uv * uvDetailScale, instanceProps[instanceId].materialProperties.filterMode, instanceProps[instanceId].materialProperties.hAddressMode, instanceProps[instanceId].materialProperties.vAddressMode).xyz;
				normalColor = (normalColor * 2.0f) - 1.0f;

				float3 newNormal = normalize(vertex.normal * normalColor.z + vertex.tangent * normalColor.x + vertex.binormal * normalColor.y);
				vertex.normal = newNormal;
			}

			vertex.normal = normalize(mul(instanceProps[instanceId].objectToWorldNormal, float4(vertex.normal, 0.f)).xyz);

			// Flip the normal if this is hitting the backface.
			vertex.triNormal = normalize(mul(instanceProps[instanceId].objectToWorldNormal, float4(vertex.triNormal, 0.f)).xyz);
			bool isBackFacing = dot(vertex.triNormal, WorldRayDirection()) > 0.0f;
			if (isBackFacing) {
				vertex.normal = -vertex.normal;
			}

			half specularColor = (1.0h);

			// Sample the specular map.
			int specularTexIndex = instanceProps[instanceId].materialProperties.specularTexIndex;
			if (specularTexIndex >= 0) {
				float uvDetailScale = instanceProps[instanceId].materialProperties.uvDetailScale;
				specularColor = half(SampleTexture(gTextures[specularTexIndex], vertex.uv * uvDetailScale, instanceProps[instanceId].materialProperties.filterMode, instanceProps[instanceId].materialProperties.hAddressMode, instanceProps[instanceId].materialProperties.vAddressMode).r);
				}

			// Store hit data and increment the hit counter.
			gHitDistance[hi] = tval;
			gHitColor[hi] = resultColor;
			gHitSpecular[hi] = specularColor;
			gHitNormal[hi] = float4(vertex.normal, 1.0f);
			gHitInstanceId[hi] = instanceId;
			
			++payload.nhits;

			if (hitPos != MAX_HIT_QUERIES - 1) {
				IgnoreHit();
			}
		}
		else {
			IgnoreHit();
		}
	}
	else {
		IgnoreHit();
	}
}

[shader("closesthit")]
void SurfaceClosestHit(inout HitInfo payload, Attributes attrib) {
	// No-op.
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}