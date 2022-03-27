//
// RT64
//

#define EPSILON 1e-6

float4 ComputeFogFromCamera(uint instanceId, float3 position) {
	float4 fogColor = float4(instanceMaterials[instanceId].fogColor, 0.0f);
	float fogMul = instanceMaterials[instanceId].fogMul;
	float fogOffset = instanceMaterials[instanceId].fogOffset;
	float4 clipPos = mul(mul(projection, view), float4(position.xyz, 1.0f));

	// Values from the game are designed around -1 to 1 space.
	clipPos.z = clipPos.z * 2.0f - clipPos.w;

	float winv = 1.0f / max(clipPos.w, 0.001f);
	const float DivisionFactor = 255.0f;
	fogColor.a = clamp((clipPos.z * winv * fogMul + fogOffset) / DivisionFactor, 0.0f, 1.0f);
	return fogColor;
}

float4 ComputeFogFromOrigin(uint instanceId, float3 position, float3 origin) {
	float4 fogColor = float4(instanceMaterials[instanceId].fogColor, 0.0f);
	float fogMul = instanceMaterials[instanceId].fogMul;
	float fogOffset = instanceMaterials[instanceId].fogOffset;
	float distance = length(position - origin);
	fogColor.a = clamp(((distance + fogOffset) / fogMul) * 0.5f, 0.0f, 1.0f);
	return fogColor;
}

// Volumetric Test Box
// First vector: Origin
// Second vector: Dimensions
static float3x2 VolumetricBox =
{
    { 0.0, 0.0, 0.0 },
    { 10.0, 10.0, 10.0 }
};

#define GROUND_HEIGHT 75.0f
#define GROUND_OFFSET 50.0f
#define GROUND_MULTIPLIER 0.50f

#define LIGHTSHAFT_SAMPLES 10
#define LIGHTSHAFT_MAX_SAMPLES 20
#define LIGHTSHAFT_STEPS 1.0f
float4 SceneGroundFogFromCamera(float3 position)
{
    float4 fogColor = float4(0.55f, 0.6f, 0.85f, 0.0f);
    if (position.y - GROUND_OFFSET < GROUND_HEIGHT)
    {
        fogColor.a = clamp(-(position.y - GROUND_OFFSET) / GROUND_HEIGHT * GROUND_MULTIPLIER, 0.f, 1.0f);
    }
    return fogColor;
}

float4 SceneGroundFogFromOrigin(float3 position, float3 origin, float cameraMul, float cameraOffset, float groundHeight, float groundOffset, float4 fogColor)
{    
    if (groundHeight > 0.f && position.y - groundOffset > groundHeight) {
        return float4(0.f, 0.f, 0.f, 0.f);
    }
    if (groundHeight < 0.f && position.y - groundOffset < groundHeight) {
        return float4(0.f, 0.f, 0.f, 0.f);
    }
    float distance = length(position - origin);
    float fogFactor = (distance - cameraOffset) / cameraMul * 0.5f;
    fogColor.a = clamp(-(position.y - groundOffset) / groundHeight * fogFactor, 0.f, 1.0f) * fogColor.a;
    return fogColor;
}

float4 SceneFogFromOrigin(float3 position, float3 origin, float cameraMul, float cameraOffset, float4 fogColor)
{    
    float distance = length(position - origin);
    float fogFactor = (distance - cameraOffset) / cameraMul * 0.5f;
    
    fogColor.a = clamp(fogFactor, 0.0f, 1.0f) * fogColor.a;
    return fogColor;
}