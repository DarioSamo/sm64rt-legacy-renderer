//
// RT64
//

#include "Instances.hlsli"
#include "Mesh.hlsli"
#include "Ray.hlsli"
#include "Samplers.hlsli"
#include "Textures.hlsli"

[shader("anyhit")]
void ShadowAnyHit(inout ShadowHitInfo payload, Attributes attrib) {
	uint instanceId = NonUniformResourceIndex(InstanceIndex());
	if (instanceProps[instanceId].ccFeatures.opt_alpha) {
		uint triangleId = PrimitiveIndex();
		float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);
		VertexAttributes vertex = GetVertexAttributes(vertexBuffer, indexBuffer, triangleId, barycentrics);
		int diffuseTexIndex = instanceProps[instanceId].materialProperties.diffuseTexIndex;
		float4 texelColor = SampleTexture(gTextures[diffuseTexIndex], vertex.uv, instanceProps[instanceId].materialProperties.filterMode, instanceProps[instanceId].materialProperties.hAddressMode, instanceProps[instanceId].materialProperties.vAddressMode);

		ColorCombinerInputs ccInputs;
		ccInputs.input1 = vertex.input[0];
		ccInputs.input2 = vertex.input[1];
		ccInputs.input3 = vertex.input[2];
		ccInputs.input4 = vertex.input[3];
		ccInputs.texVal0 = texelColor;
		ccInputs.texVal1 = texelColor;

		float resultAlpha = clamp(CombineColors(instanceProps[instanceId].ccFeatures, ccInputs).a * instanceProps[instanceId].materialProperties.shadowAlphaMultiplier, 0.0f, 1.0f);
		payload.shadowHit = max(payload.shadowHit - resultAlpha, 0.0f);
		if (payload.shadowHit > 0.0f) {
			IgnoreHit();
		}
	}
	else {
		payload.shadowHit = 0.0f;
	}
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowHitInfo payload, Attributes attrib) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}