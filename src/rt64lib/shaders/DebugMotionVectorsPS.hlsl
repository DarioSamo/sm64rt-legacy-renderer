//
// RT64
//

// Adapted from https://www.shadertoy.com/view/3dlGDM

#include "GlobalBuffers.hlsli"
#include "GlobalParams.hlsli"

float2 getPreviousFrameUVs(float2 pos) {
    uint2 posInt = uint2(round(pos));
    float2 flow = gFlow[posInt].xy;
    return pos + flow;
}

float distanceFromLineSegment(float2 p, float2 start, float2 end) {
    float len = length(start - end);
    float l2 = len * len;
    if (l2 == 0.0f) {
        return length(p - start);
    }

    float t = max(0.0f, min(1.0f, dot(p - start, end - start) / l2));
    float2 projection = start + t * (end - start);
    return length(p - projection);
}

float4 motionVector(float2 pos) {
    float lineThickness = 1.0f;
    float blockSize = 32.0f;

    // Divide the frame into blocks of "blockSize x blockSize" pixels
    // and get the screen coordinates of the pixel in the center of closest block.
    float2 currentCenterPos = floor(pos / blockSize) * blockSize + (blockSize * 0.5f);

    // Load motion vector for pixel in the center of the block.
    float2 previousCenterPos = getPreviousFrameUVs(currentCenterPos);

    // Get distance of this pixel from motion vector line on the screen.
    float lineDistance = distanceFromLineSegment(pos, currentCenterPos, previousCenterPos);

    // Draw line based on distance.
    return (lineDistance < lineThickness) ? float4(1.0f, 1.0f, 1.0f, 1.0f) : float4(0.0f, 0.0f, 0.0f, 0.0f);
}

float4 PSMain(in float4 pos : SV_Position, in float2 uv : TEXCOORD0) : SV_TARGET {
    return motionVector(uv * resolution.xy);
}