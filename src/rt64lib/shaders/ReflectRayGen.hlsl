//
// RT64
//

#include "Ray.hlsli"

[shader("raygeneration")]
void TraceRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}