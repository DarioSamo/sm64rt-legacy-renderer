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
	int diffuseTexIndex = instanceMaterials[instanceId].materialProperties.diffuseTexIndex;
	float4 diffuseColorMix = instanceMaterials[instanceId].materialProperties.diffuseColorMix;
	float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);
	VertexAttributes vertex = GetVertexAttributes(vertexBuffer, indexBuffer, triangleId, barycentrics);
	float4 texelColor = SampleTexture(gTextures[diffuseTexIndex], vertex.uv, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode);

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
	float4 resultColor = CombineColors(instanceMaterials[instanceId].ccFeatures, ccInputs, seed);
	resultColor.a = clamp(instanceMaterials[instanceId].materialProperties.solidAlphaMultiplier * resultColor.a, 0.0f, 1.0f);

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

			int normalTexIndex = instanceMaterials[instanceId].materialProperties.normalTexIndex;
			if (normalTexIndex >= 0) {
				float uvDetailScale = instanceMaterials[instanceId].materialProperties.uvDetailScale;
				float3 normalColor = SampleTexture(gTextures[normalTexIndex], vertex.uv * uvDetailScale, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode).xyz;
				normalColor = (normalColor * 2.0f) - 1.0f;

				float3 newNormal = normalize(vertex.normal * normalColor.z + vertex.tangent * normalColor.x + vertex.binormal * normalColor.y);
				vertex.normal = newNormal;
			}

			vertex.normal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertex.normal, 0.f)).xyz);

			// Flip the normal if this is hitting the backface.
			vertex.triNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertex.triNormal, 0.f)).xyz);
			bool isBackFacing = dot(vertex.triNormal, WorldRayDirection()) > 0.0f;
			if (isBackFacing) {
				vertex.normal = -vertex.normal;
			}

			// Sample the specular map.
			float specularColor = 1.0f;
			int specularTexIndex = instanceMaterials[instanceId].materialProperties.specularTexIndex;
			if (specularTexIndex >= 0) {
				float uvDetailScale = instanceMaterials[instanceId].materialProperties.uvDetailScale;
				specularColor = SampleTexture(gTextures[specularTexIndex], vertex.uv * uvDetailScale, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode).r;
			}

			// Store hit data and increment the hit counter.
			gHitDistance[hi] = tval;
			gHitColor[hi] = resultColor;
			gHitNormal[hi] = float4(vertex.normal, 1.0f);
			gHitSpecular[hi] = specularColor;
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