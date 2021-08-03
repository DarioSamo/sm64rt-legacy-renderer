//
// RT64
//

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