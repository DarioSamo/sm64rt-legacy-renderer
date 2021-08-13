//
// RT64
//

Texture2D<float4> gBlueNoise : register(t7);

float3 getBlueNoise(uint2 pixelPos, uint frameCount) {
	uint2 blueNoiseBase;
	uint blueNoiseFrame = frameCount % 64;
	blueNoiseBase.x = (blueNoiseFrame % 8) * 64;
	blueNoiseBase.y = (blueNoiseFrame / 8) * 64;
	return gBlueNoise.Load(uint3(blueNoiseBase + pixelPos % 64, 0)).rgb;
}