//
// RT64
//

// Parameters for root signature.

ByteAddressBuffer vertexBuffer : register(t3);
ByteAddressBuffer indexBuffer : register(t4);

struct VertexAttributes {
	float3 position;
	float3 normal;
	float3 triNormal;
	float3 tangent;
	float3 binormal;
	float2 uv;
	float4 input[4];
};

#define VERTEX_BUFFER_FLOAT_COUNT 24

uint3 GetIndices(ByteAddressBuffer indexBuffer, uint triangleIndex) {
	int address = (triangleIndex * 3) * 4;
	return indexBuffer.Load3(address);
}

uint PosAddr(uint index) {
	return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4);
}

uint NormAddr(uint index) {
	return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4) + 12;
}

uint UvAddr(uint index) {
	return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4) + 24;
}

uint InputAddr(uint index, uint inputIndex) {
	return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4) + (inputIndex * 16) + 32;
}

float3 ProjectOnPlane(float3 position, float3 origin, float3 normal) {
	return position - dot(position - origin, normal) * normal;
}

VertexAttributes GetVertexAttributes(ByteAddressBuffer vertexBuffer, ByteAddressBuffer indexBuffer, uint triangleIndex, float3 barycentrics) {
	uint3 index3 = GetIndices(indexBuffer, triangleIndex);
	VertexAttributes v;

	float3 pos0 = asfloat(vertexBuffer.Load3(PosAddr(index3[0])));
	float3 pos1 = asfloat(vertexBuffer.Load3(PosAddr(index3[1])));
	float3 pos2 = asfloat(vertexBuffer.Load3(PosAddr(index3[2])));
	v.position = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];

	float3 norm0 = asfloat(vertexBuffer.Load3(NormAddr(index3[0])));
	float3 norm1 = asfloat(vertexBuffer.Load3(NormAddr(index3[1])));
	float3 norm2 = asfloat(vertexBuffer.Load3(NormAddr(index3[2])));
	float3 vertNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];
	v.triNormal = -cross(pos2 - pos0, pos1 - pos0);
	v.normal = any(vertNormal) ? normalize(vertNormal) : v.triNormal;

	float2 uv0 = asfloat(vertexBuffer.Load2(UvAddr(index3[0])));
	float2 uv1 = asfloat(vertexBuffer.Load2(UvAddr(index3[1])));
	float2 uv2 = asfloat(vertexBuffer.Load2(UvAddr(index3[2])));
	v.uv = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];

	for (int i = 0; i < 4; i++) {
		v.input[i] =
			asfloat(vertexBuffer.Load4(InputAddr(index3[0], i))) * barycentrics[0] +
			asfloat(vertexBuffer.Load4(InputAddr(index3[1], i))) * barycentrics[1] +
			asfloat(vertexBuffer.Load4(InputAddr(index3[2], i))) * barycentrics[2];
	}

	///
	// Compute the tangent vector for the polygon.
	// Derived from http://area.autodesk.com/blogs/the-3ds-max-blog/how_the_3ds_max_scanline_renderer_computes_tangent_and_binormal_vectors_for_normal_mapping
	float uva = uv1.x - uv0.x;
	float uvb = uv2.x - uv0.x;
	float uvc = uv1.y - uv0.y;
	float uvd = uv2.y - uv0.y;
	float uvk = uvb * uvc - uva * uvd;
	float3 dpos1 = pos1 - pos0;
	float3 dpos2 = pos2 - pos0;

	// TODO: Evaluate how to accomodate this to the smoothed tangent if it exists, probably by calculating the cross vector manually?
	// Also only do this if it actually uses a normal map? Would be better to move this to the shader instead?
	if (uvk != 0) {
		v.tangent = normalize((uvc * dpos2 - uvd * dpos1) / uvk);
	}
	else {
		if (uva != 0) {
			v.tangent = normalize(dpos1 / uva);
		}
		else if (uvb != 0) {
			v.tangent = normalize(dpos2 / uvb);
		}
		else {
			v.tangent = 0.0f;
		}
	}

	// Calculate the W component of the tangent.
	// Consider V component inverted for calculating this just like in 3DS Max.
	float2 duv1 = uv1 - uv0;
	float2 duv2 = uv2 - uv1;
	duv1.y = -duv1.y;
	duv2.y = -duv2.y;
	float3 cr = cross(float3(duv1.xy, 0.0f), float3(duv2.xy, 0.0f));
	float binormalMult = (cr.z < 0.0f) ? -1.0f : 1.0f;
	v.binormal = cross(v.tangent, v.normal) * binormalMult;
	///

	return v;
}
