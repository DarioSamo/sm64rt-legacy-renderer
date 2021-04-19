//
// RT64
//

#include "GlobalBuffers.hlsli"
#include "ViewParams.hlsli"

[numthreads(1, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    int gIndex = DTid.y * (int)(viewport.z) + DTid.x;
    float4 dColor = gDenoised[gIndex];
    float4 fgColor = gForeground[DTid.xy];
    gOutput[DTid.xy] = lerp(dColor, fgColor, fgColor.a);
}