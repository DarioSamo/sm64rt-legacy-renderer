/*
*   
*   RT64
*   Luminance Histogram (average pass) by Alex Tardif
*   From http://www.alextardif.com/HistogramLuminance.html
*   
*/

#define NUM_HISTOGRAM_BINS 64
#define HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION 8

RWByteAddressBuffer LuminanceHistogram : register(u0);
RWTexture2D<float> LuminanceOutput : register(u1);

cbuffer LuminanceHistogramAverageBuffer : register(b0)
{
	uint pixelCount;
	float minLogLuminance;
	float logLuminanceRange;
	float timeDelta;
	float tau;
};

groupshared float HistogramShared[NUM_HISTOGRAM_BINS];

[numthreads(HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, 1)]
void mainCS(uint groupIndex : SV_GroupIndex)
{
	float countForThisBin = (float) LuminanceHistogram.Load(groupIndex * 4);
	HistogramShared[groupIndex] = countForThisBin * (float) groupIndex;
	GroupMemoryBarrierWithGroupSync();
	
	[unroll]
	for (uint histogramSampleIndex = (NUM_HISTOGRAM_BINS >> 1); histogramSampleIndex > 0; histogramSampleIndex >>= 1) {
		if (groupIndex < histogramSampleIndex) {
			HistogramShared[groupIndex] += HistogramShared[groupIndex + histogramSampleIndex];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	
	if (groupIndex == 0) {
		float weightedLogAverage = (HistogramShared[0].x / max((float) pixelCount - countForThisBin, 1.0)) - 1.0;
        float weightedAverageLuminance = exp2(((weightedLogAverage / 62.0) * logLuminanceRange) + minLogLuminance);
		float luminanceLastFrame = LuminanceOutput[uint2(0, 0)];
		if (isnan(luminanceLastFrame) || isinf(luminanceLastFrame)) {
			luminanceLastFrame = 0.5f;
		}
		float adaptedLuminance = luminanceLastFrame + (weightedAverageLuminance - luminanceLastFrame) * (1 - exp(-timeDelta * tau));
        LuminanceOutput[uint2(0, 0)] = adaptedLuminance;
    }
}