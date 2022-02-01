/*
*   
*   RT64
*   Luminance Histogram by Alex Tardif
*   From http://www.alextardif.com/HistogramLuminance.html
*   
*/

#define NUM_HISTOGRAM_BINS 64
#define HISTOGRAM_THREADS_PER_DIMENSION 8
#define EPSILON	1e-6

Texture2D<float3> HDRTexture : register(t0);
RWByteAddressBuffer LuminanceHistogram : register(u0);

cbuffer LuminanceHistogramBuffer : register(b0)
{
	uint inputWidth;
	uint inputHeight;
	float minLogLuminance;
	float oneOverLogLuminanceRange;
};

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

float GetLuminance(float3 color) {
	return dot(color, float3(0.2127f, 0.7152f, 0.0722f));
}

uint HDRToHistogramBin(float3 hdrColor)
{
	float luminance = GetLuminance(hdrColor);
	if (luminance < EPSILON) {
		return 0;
	}
	
	float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
	return (uint) (logLuminance * 62.0 + 1.0);
}

[numthreads(HISTOGRAM_THREADS_PER_DIMENSION, HISTOGRAM_THREADS_PER_DIMENSION, 1)]
void mainCS(uint groupIndex : SV_GroupIndex, uint3 threadId : SV_DispatchThreadID)
{
	HistogramShared[groupIndex] = 0;
	
	GroupMemoryBarrierWithGroupSync();
	if (threadId.x < inputWidth && threadId.y < inputHeight) {
		float3 hdrColor = HDRTexture.Load(int3(threadId.xy, 0)).rgb;
		uint binIndex = HDRToHistogramBin(hdrColor);
		InterlockedAdd(HistogramShared[binIndex], 1);
	}
	GroupMemoryBarrierWithGroupSync();
	
	LuminanceHistogram.InterlockedAdd(groupIndex * 4, HistogramShared[groupIndex]);
}