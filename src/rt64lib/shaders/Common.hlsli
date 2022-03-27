//
// RT64
//

float3 getCosHemisphereSampleBlueNoise(uint2 pixelPos, uint frameCount, float roughness, float3 hitNorm)
{
    float2 randVal = getBlueNoise(pixelPos, frameCount).rg;

	// Cosine weighted hemisphere sample from RNG
    float3 bitangent = getPerpendicularVector(hitNorm);
    float3 tangent = cross(bitangent, hitNorm);
    float h = sqrt(max(0.0f, (1.0f - randVal.x) / ((roughness * roughness - 1.0f) * randVal.x + 1)));
    float r = sqrt(max(0.0f, 1.0f - h * h));
    float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
    return tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + hitNorm.xyz * h;
}