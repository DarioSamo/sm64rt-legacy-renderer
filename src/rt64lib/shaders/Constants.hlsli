//
// RT64
//

#define EPSILON								1e-6
#define M_PI								3.14159265f
#define M_TWO_PI							(M_PI * 2.0f)
#define APPLY_LIGHTS_MINIMUM_ALPHA			0.5

// Got this from https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// About that beer I owed ya...
// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    { 0.59719, 0.35458, 0.04823 },
    { 0.07600, 0.90834, 0.01566 },
    { 0.02840, 0.13383, 0.83777 }
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367 },
    { -0.10208, 1.10813, -0.00605 },
    { -0.00327, -0.07276, 1.07602 }
};