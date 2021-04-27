//
// Im3d Dx12
//

#include "Im3DCommon.hlsli"
#include "GlobalHitBuffers.hlsli"
#include "ViewParams.hlsli"

float4 PSMain(VS_OUTPUT _in) : SV_Target {
	uint2 pixelDims = round(resolution.xy);
	uint2 pixelPos = clamp(_in.m_position.xy, uint2(0, 0), pixelDims);
	uint hitBufferIndex = getHitBufferIndex(0, pixelPos, pixelDims);
	float occlDistance = gHitDistance[hitBufferIndex];
	float3 viewPos = mul(viewI, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
	float pixelDistance = length(_in.m_worldPosition - viewPos);
	float4 ret = _in.m_color;

	// Dither and make the pixels more transparent if occluded.
	if (occlDistance < pixelDistance) {
		ret.a *= 0.5f;
		clip(fmod(_in.m_position.x + _in.m_position.y, 2.0f) - 1.0f);
	}
	
	return ret;
}