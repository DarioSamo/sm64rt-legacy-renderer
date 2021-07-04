//
// RT64
//

// TODO: This code was recreated from the decompilation.
// It can probably be simplified with a proper rewrite.
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SKYBOX_WIDTH (4 * SCREEN_WIDTH)
#define SKYBOX_HEIGHT (4 * SCREEN_HEIGHT)

float2 ComputeSkyPlaneUV(float2 uv, float4x4 viewI, float2 viewportSz) {
	float2 baseUV = float2(0.0f, 0.0f);

	// Determine vertex UV.
	float3 viewDirection = normalize(mul(viewI, float4(0, 0, 1, 0)).xyz);

	// Scaled X
	float skyYawRadians = atan2(viewDirection.x, -viewDirection.z);
	baseUV.x = SCREEN_WIDTH * 360.0 * skyYawRadians / (90.0f * M_PI * 2.0f);

	// Scaled Y
	float skyPitchRadians = atan2(-viewDirection.y, sqrt(viewDirection.x * viewDirection.x + viewDirection.z * viewDirection.z));
	float pitchInDegrees = skyPitchRadians * 360.0f / (M_PI * 2.0f);
	float degreesToScale = 360.0f * pitchInDegrees / 90.0f;
	baseUV.y = degreesToScale + 5.0f * (SCREEN_HEIGHT / 2.0f);
	baseUV.y = clamp(baseUV.y, SCREEN_HEIGHT, SKYBOX_HEIGHT);

	// Adapt to the aspect ratio.
	float aspectRatio = viewportSz.x / viewportSz.y;
	baseUV.x += SCREEN_WIDTH / 2.0f;
	baseUV.x -= (SCREEN_HEIGHT * aspectRatio) / 2.0f;

	// Convert to the texture space.
	baseUV.x /= SKYBOX_WIDTH;
	baseUV.y = (SKYBOX_HEIGHT - baseUV.y) / SKYBOX_HEIGHT;

	// Add the specified UV to the base UV.
	float ratioDivision = aspectRatio / (4.0f / 3.0f);
	baseUV.x += uv.x * 0.25f * ratioDivision;
	baseUV.y += uv.y * 0.25f;

	return baseUV;
}