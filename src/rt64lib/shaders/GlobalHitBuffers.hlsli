//
// RT64
//

#define MAX_HIT_QUERIES	12

RWBuffer<float> gHitDistance : register(u5);
RWBuffer<half4> gHitColor : register(u6);
RWBuffer<half4> gHitNormal : register(u7);
RWBuffer<uint> gHitInstanceId : register(u8);

uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {
	return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;
}