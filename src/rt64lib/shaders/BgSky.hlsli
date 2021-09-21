//
// RT64
//

SamplerState gBackgroundSampler : register(s0);

// TODO: This code was recreated from the decompilation.
// It can probably be simplified with a proper rewrite.
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SKYBOX_WIDTH (4 * SCREEN_WIDTH)
#define SKYBOX_HEIGHT (4 * SCREEN_HEIGHT)

float2 FakeEnvMapUV(float3 rayDirection, float yawOffset) {
	float yaw = fmod(yawOffset + atan2(rayDirection.x, -rayDirection.z) + M_PI, M_TWO_PI);
	float pitch = fmod(atan2(-rayDirection.y, sqrt(rayDirection.x * rayDirection.x + rayDirection.z * rayDirection.z)) + M_PI, M_TWO_PI);
	return float2(yaw / M_TWO_PI, pitch / M_TWO_PI);
}

float2 ComputeSkyPlaneUV(float2 uv, float4x4 viewI, float2 viewportSz, float yawOffset) {
	float2 baseUV = float2(0.0f, 0.0f);

	// Determine vertex UV.
	float3 viewDirection = normalize(mul(viewI, float4(0, 0, 1, 0)).xyz);

	// Scaled X
	float skyYawRadians = fmod(yawOffset + atan2(viewDirection.x, -viewDirection.z) + M_PI, M_TWO_PI);
	baseUV.x = SCREEN_WIDTH * 360.0 * (skyYawRadians - M_PI) / (90.0f * M_PI * 2.0f);

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

float4 SampleSky2D(float2 screenUV) {
	if (skyPlaneTexIndex >= 0) {
		float2 skyUV = ComputeSkyPlaneUV(screenUV, viewI, viewport.zw, skyYawOffset);
		float4 skyColor = gTextures[skyPlaneTexIndex].SampleLevel(gBackgroundSampler, skyUV, 0);
		skyColor.rgb *= skyDiffuseMultiplier.rgb;

		if (any(skyHSLModifier)) {
			skyColor.rgb = ModRGBWithHSL(skyColor.rgb, skyHSLModifier.rgb);
		}

		return skyColor;
	}
	else {
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

float4 SampleSkyPlane(float3 rayDirection) {
	if (skyPlaneTexIndex >= 0) {
		float4 skyColor = gTextures[skyPlaneTexIndex].SampleLevel(gBackgroundSampler, FakeEnvMapUV(rayDirection, skyYawOffset), 0);
		skyColor.rgb *= skyDiffuseMultiplier.rgb;

		if (any(skyHSLModifier)) {
			skyColor.rgb = ModRGBWithHSL(skyColor.rgb, skyHSLModifier.rgb);
		}

		return skyColor;
	}
	else {
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

float3 SampleBackground2D(float2 screenUV) {
	return gBackground.SampleLevel(gBackgroundSampler, screenUV, 0).rgb;
}

float3 SampleBackgroundAsEnvMap(float3 rayDirection) {
	return gBackground.SampleLevel(gBackgroundSampler, FakeEnvMapUV(rayDirection, 0.0f), 0).rgb;
}