//
// RT64
//

float3 microfacetGGX(uint2 pixelPos, uint frameCount, float roughness, float3 normal)
{
    float2 randVal = getBlueNoise(pixelPos, frameCount).rg;
    float3 binormal = getPerpendicularVector(normal);
    float3 tangent = cross(binormal, normal);
	
    float a = roughness * roughness;
    float a2 = a * a;
    float cosThetaH = sqrt(max(0.0f, (1.0f - randVal.x) / ((a2 - 1.0f) * randVal.x + 1)));
    float sinThetaH = sqrt(max(0.0f, 1.0f - cosThetaH * cosThetaH));
    float phiH = randVal.y * 3.14159265f * 2.0f;

    return tangent * (sinThetaH * cos(phiH)) + binormal * (sinThetaH * sin(phiH)) + normal * cosThetaH;
}

float MetalAmount(float colorA, float colorB, float metalness)
{
    if (colorB >= EPSILON) {
        return colorA * pow(colorB, metalness);
    } else {
        return colorA * (1.0 - metalness);
    }
}