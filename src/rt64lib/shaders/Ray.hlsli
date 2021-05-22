//
// RT64
//

#ifdef SHADER_AS_STRING
R"raw(
#else
// Structures

struct HitInfo {
	uint nhits;
	uint ohits;
};

struct Attributes {
	float2 bary;
};

struct ShadowHitInfo {
	float shadowHit;
};

// Root signature

RaytracingAccelerationStructure SceneBVH : register(t0);
//)raw"
#endif