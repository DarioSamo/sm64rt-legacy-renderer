//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
#define MAX_HIT_QUERIES	16

RWBuffer<float4> gHitDistAndFlow : register(u23);
RWBuffer<float4> gHitColor : register(u24);
RWBuffer<float4> gHitNormal : register(u25);
RWBuffer<float4> gHitSpecular : register(u26);
RWBuffer<uint> gHitInstanceId : register(u27);

uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {
	return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;
}
//)raw"
#endif