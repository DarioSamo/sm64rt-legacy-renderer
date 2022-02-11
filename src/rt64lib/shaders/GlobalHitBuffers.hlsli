//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
#define MAX_HIT_QUERIES	16

RWBuffer<float4> gHitDistAndFlow : register(u28);
RWBuffer<float4> gHitColor : register(u29);
RWBuffer<float4> gHitNormal : register(u30);
RWBuffer<float4> gHitSpecular : register(u31);
RWBuffer<uint> gHitInstanceId : register(u32);
RWBuffer<float4> gHitEmissive : register(u33);
RWBuffer<float> gHitRoughness : register(u34);
RWBuffer<float> gHitMetalness : register(u35);
RWBuffer<float> gHitAmbient : register(u36);

uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {
	return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;
}
//)raw"
#endif