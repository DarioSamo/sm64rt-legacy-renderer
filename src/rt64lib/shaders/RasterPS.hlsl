//
// RT64
//

#include "Instances.hlsli"
#include "Samplers.hlsli"
#include "Textures.hlsli"
#include "PSInput.hlsli"

int instanceIndex : register(b0);

float4 PSMain(PSInput input) : SV_TARGET {
    int instanceId = NonUniformResourceIndex(instanceIndex);
    int diffuseTexIndex = instanceProps[instanceId].materialProperties.diffuseTexIndex;
    float4 texelColor = SampleTexture(gTextures[diffuseTexIndex], input.uv, instanceProps[instanceId].materialProperties.filterMode, instanceProps[instanceId].materialProperties.hAddressMode, instanceProps[instanceId].materialProperties.vAddressMode);
    ColorCombinerInputs ccInputs;
    ccInputs.input1 = input.input1;
    ccInputs.input2 = input.input2;
    ccInputs.input3 = input.input3;
    ccInputs.input4 = input.input4;
    ccInputs.texVal0 = texelColor;
    ccInputs.texVal1 = texelColor;

    float4 resultColor = CombineColors(instanceProps[instanceId].ccFeatures, ccInputs);
    return resultColor;
}
