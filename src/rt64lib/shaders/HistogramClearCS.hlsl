/*
*   
*   RT64
*   Luminance Histogram (clear pass) by Alex Tardif
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

[numthreads(HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, 1)]
void mainCS(uint3 threadId : SV_DispatchThreadID) {
    LuminanceHistogram.Store(threadId.x * threadId.y, 0);
}