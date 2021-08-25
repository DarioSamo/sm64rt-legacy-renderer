//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
#define MAX_HIT_QUERIES	16

RWBuffer<float4> gHitDistAndFlow : register(u18);
RWBuffer<float4> gHitColor : register(u19);
RWBuffer<float4> gHitNormal : register(u20);
RWBuffer<float4> gHitSpecular : register(u21);
RWBuffer<uint> gHitInstanceId : register(u22);

uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {
	return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;
}
//)raw"
#endif