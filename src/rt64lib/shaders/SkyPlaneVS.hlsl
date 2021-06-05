//
// RT64
//

#include "Constants.hlsli"
#include "ViewParams.hlsli"

// TODO: This code was recreated from the decompilation.
// It can probably be simplified with a proper rewrite.
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SKYBOX_WIDTH (4 * SCREEN_WIDTH)
#define SKYBOX_HEIGHT (4 * SCREEN_HEIGHT)

void VSMain(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
	// Determine vertex position.
	pos.x = (id == 2) ? 3.0f : -1.0f;
	pos.y = (id == 1) ? -3.0f : 1.0f;
	pos.z = 1.0f;
	pos.w = 1.0f;

	// Determine vertex UV.
	float3 viewDirection = normalize(mul(viewI, float4(0, 0, 1, 0)).xyz);

	// Scaled X
	float skyYawRadians = atan2(viewDirection.x, -viewDirection.z);
	uv.x = SCREEN_WIDTH * 360.0 * skyYawRadians / (90.0f * M_PI * 2.0f);

	// Scaled Y
	float skyPitchRadians = atan2(-viewDirection.y, sqrt(viewDirection.x * viewDirection.x + viewDirection.z * viewDirection.z));
	float pitchInDegrees = skyPitchRadians * 360.0f / (M_PI * 2.0f);
	float degreesToScale = 360.0f * pitchInDegrees / 90.0f;
	uv.y = degreesToScale + 5.0f * (SCREEN_HEIGHT / 2.0f);
	uv.y = clamp(uv.y, SCREEN_HEIGHT, SKYBOX_HEIGHT);

	// Adapt to the aspect ratio.
	float aspectRatio = viewport.z / viewport.w;
	uv.x += SCREEN_WIDTH / 2.0f;
	uv.x -= (SCREEN_HEIGHT * aspectRatio) / 2.0f;

	// Convert to the texture space.
	uv.x /= SKYBOX_WIDTH;
	uv.y = (SKYBOX_HEIGHT - uv.y) / SKYBOX_HEIGHT;

	float ratioDivision = aspectRatio / (4.0f / 3.0f);
    uv.x += (id == 2) ? 0.5f * ratioDivision : 0.0f;
    uv.y += (id == 1) ? 0.5f : 0.0f;
}