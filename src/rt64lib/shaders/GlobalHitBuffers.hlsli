//
// RT64
//

#define MAX_HIT_QUERIES	16

RWBuffer<float> gHitDistance : register(u3);
RWBuffer<float4> gHitColor : register(u4);
RWBuffer<float4> gHitNormal : register(u5);
RWBuffer<float> gHitSpecular : register(u6);
RWBuffer<uint> gHitInstanceId : register(u7);

uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {
	return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;
}