//
// RT64
//

#define MAX_HIT_QUERIES	12

RWBuffer<float>  gHitDistance : register(u1);
RWBuffer<half4> gHitColor : register(u2);
RWBuffer<half4> gHitNormal : register(u3);
RWBuffer<uint> gHitInstanceId : register(u4);

uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {
	return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;
}