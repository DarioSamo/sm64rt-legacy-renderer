//
// RT64
//

cbuffer CameraParams : register(b0) {
	float4x4 view;
	float4x4 projection;
	float4x4 viewI;
	float4x4 projectionI;
	float4x4 prevViewProj;
	float4x4 curViewProj;
}