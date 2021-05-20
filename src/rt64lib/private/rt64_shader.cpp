//
// RT64
//
#ifndef RT64_MINIMAL

#include "rt64_shader.h"

#include "rt64_device.h"

#include "utf8conv/utf8conv.h"

// Private

RT64::Shader::Shader(Device *device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr) {
	assert(device != nullptr);
	this->device = device;

	// Generate shader with unique name based on the shader ID, the filtering and the addressing modes.
	const std::string baseName = "Shader_" + std::to_string(shaderId) + "_" + std::to_string(uniqueSamplerRegisterIndex(filter, hAddr, vAddr));
	const std::string hitGroup = baseName + "HitGroup";
	const std::string closestHit = baseName + "ClosestHit";
	const std::string anyHit = baseName + "AnyHit";
	const std::string shadowHitGroup = baseName + "ShadowHitGroup";
	const std::string shadowClosestHit = baseName + "ShadowClosestHit";
	const std::string shadowAnyHit = baseName + "ShadowAnyHit";
	generateSurfaceHitGroup(shaderId, filter, hAddr, vAddr, hitGroup, closestHit, anyHit);
	generateShadowHitGroup(shaderId, filter, hAddr, vAddr, shadowHitGroup, shadowClosestHit, shadowAnyHit);
	device->addShader(this);
}

RT64::Shader::~Shader() {
	device->removeShader(this);
    surfaceHitGroup.shaderBlob->Release();
	shadowHitGroup.shaderBlob->Release();
}

#define SS(x) ss << x << std::endl;

unsigned int RT64::Shader::uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr) {
	unsigned int uniqueID = 32;
	uniqueID += (unsigned int)(filter) * 9;
	uniqueID += (unsigned int)(hAddr) * 3;
	uniqueID += (unsigned int)(vAddr);
	return uniqueID;
}

void incMeshBuffers(std::stringstream &ss) {
	SS("ByteAddressBuffer vertexBuffer : register(t2);");
	SS("ByteAddressBuffer indexBuffer : register(t3);");
}

void incMeshFunctions(std::stringstream &ss) {
	SS("#define VERTEX_BUFFER_FLOAT_COUNT 24");
	SS("uint3 GetIndices(ByteAddressBuffer indexBuffer, uint triangleIndex) {");
	SS("    int address = (triangleIndex * 3) * 4;");
	SS("    return indexBuffer.Load3(address);");
	SS("}");
	SS("uint PosAddr(uint index) {");
	SS("    return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4);");
	SS("}");
	SS("uint NormAddr(uint index) {");
	SS("    return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4) + 12;");
	SS("}");
	SS("uint UvAddr(uint index) {");
	SS("    return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4) + 24;");
	SS("}");
	SS("uint InputAddr(uint index, uint inputIndex) {");
	SS("    return ((index * VERTEX_BUFFER_FLOAT_COUNT) * 4) + (inputIndex * 16) + 32;");
	SS("}");
}

void incInstanceBuffers(std::stringstream &ss) {
	SS("struct InstanceTransforms {");
	SS("    float4x4 objectToWorld;");
	SS("    float4x4 objectToWorldNormal;");
	SS("};");
	SS("struct MaterialProperties {");
	SS("    int filterMode;");
	SS("    int diffuseTexIndex;");
	SS("    int normalTexIndex;");
	SS("    int specularTexIndex;");
	SS("    int hAddressMode;");
	SS("    int vAddressMode;");
	SS("    float ignoreNormalFactor;");
	SS("    float uvDetailScale;");
	SS("    float reflectionFactor;");
	SS("    float reflectionFresnelFactor;");
	SS("    float reflectionShineFactor;");
	SS("    float refractionFactor;");
	SS("    float specularIntensity;");
	SS("    float specularExponent;");
	SS("    float solidAlphaMultiplier;");
	SS("    float shadowAlphaMultiplier;");
	SS("    float depthBias;");
	SS("    float shadowRayBias;");
	SS("    float3 selfLight;");
	SS("    uint lightGroupMaskBits;");
	SS("    float3 fogColor;");
	SS("    float4 diffuseColorMix;");
	SS("    float fogMul;");
	SS("    float fogOffset;");
	SS("    uint _pad;");
	SS("};");
	SS("struct ColorCombinerFeatures {");
	SS("	int4 c0;");
	SS("	int4 c1;");
	SS("	int2 do_single;");
	SS("	int2 do_multiply;");
	SS("	int2 do_mix;");
	SS("	int color_alpha_same;");
	SS("	int opt_alpha;");
	SS("	int opt_fog;");
	SS("	int opt_texture_edge;");
	SS("	int opt_noise;");
	SS("	float _padding;");
	SS("};");
	SS("struct InstanceMaterials {");
	SS("	MaterialProperties materialProperties;");
	SS("	ColorCombinerFeatures ccFeatures;");
	SS("};");
	SS("StructuredBuffer<InstanceTransforms> instanceTransforms : register(t5);");
	SS("StructuredBuffer<InstanceMaterials> instanceMaterials : register(t6);");
}

void incRayAttributes(std::stringstream &ss) {
	SS("struct Attributes {");
	SS("    float2 bary;");
	SS("};");
}

void incGlobalHitBuffers(std::stringstream &ss) {
	SS("#define MAX_HIT_QUERIES 16");
	SS("RWBuffer<float> gHitDistance : register(u3);");
	SS("RWBuffer<float4> gHitColor : register(u4);");
	SS("RWBuffer<float4> gHitNormal : register(u5);");
	SS("RWBuffer<float> gHitSpecular : register(u6);");
	SS("RWBuffer<uint> gHitInstanceId : register(u7);");
	SS("uint getHitBufferIndex(uint hitPos, uint2 pixelIdx, uint2 pixelDims) {");
	SS("    return (hitPos * pixelDims.y + pixelIdx.y) * pixelDims.x + pixelIdx.x;");
	SS("}");
}

void getVertexData(std::stringstream &ss, bool vertexPosition, bool vertexNormal, bool vertexUV, int inputCount) {
	SS("uint3 index3 = GetIndices(indexBuffer, triangleIndex);");

	if (vertexPosition) {
		SS("float3 pos0 = asfloat(vertexBuffer.Load3(PosAddr(index3[0])));");
		SS("float3 pos1 = asfloat(vertexBuffer.Load3(PosAddr(index3[1])));");
		SS("float3 pos2 = asfloat(vertexBuffer.Load3(PosAddr(index3[2])));");
		SS("float3 vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];");
	}

	if (vertexNormal) {
		SS("float3 norm0 = asfloat(vertexBuffer.Load3(NormAddr(index3[0])));");
		SS("float3 norm1 = asfloat(vertexBuffer.Load3(NormAddr(index3[1])));");
		SS("float3 norm2 = asfloat(vertexBuffer.Load3(NormAddr(index3[2])));");
		SS("float3 vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];");
		SS("float3 triangleNormal = -cross(pos2 - pos0, pos1 - pos0);");
		SS("vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : triangleNormal;");
		SS("vertexNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexNormal, 0.f)).xyz);");
		SS("triangleNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(triangleNormal, 0.f)).xyz);");
		SS("bool isBackFacing = dot(triangleNormal, WorldRayDirection()) > 0.0f;");
		SS("if (isBackFacing) { vertexNormal = -vertexNormal; }");
	}

	if (vertexUV) {
		SS("float2 uv0 = asfloat(vertexBuffer.Load2(UvAddr(index3[0])));");
		SS("float2 uv1 = asfloat(vertexBuffer.Load2(UvAddr(index3[1])));");
		SS("float2 uv2 = asfloat(vertexBuffer.Load2(UvAddr(index3[2])));");
		SS("float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];");
	}

	for (int i = 0; i < inputCount; i++) {
		SS("float4 input" + std::to_string(i + 1) + " = "
			"asfloat(vertexBuffer.Load4(InputAddr(index3[0], " + std::to_string(i) + "))) * barycentrics[0] + "
			"asfloat(vertexBuffer.Load4(InputAddr(index3[1], " + std::to_string(i) + "))) * barycentrics[1] + "
			"asfloat(vertexBuffer.Load4(InputAddr(index3[2], " + std::to_string(i) + "))) * barycentrics[2];");
	}
}

void incTextures(std::stringstream &ss) {
	SS("Texture2D<float4> gTextures[1024] : register(t7);");
}

enum {
	SHADER_0,
	SHADER_INPUT_1,
	SHADER_INPUT_2,
	SHADER_INPUT_3,
	SHADER_INPUT_4,
	SHADER_TEXEL0,
	SHADER_TEXEL0A,
	SHADER_TEXEL1
};

std::string colorInput(int item, bool with_alpha, bool inputs_have_alpha, bool hint_single_element) {
	switch (item) {
	default:
	case SHADER_0:
		return with_alpha ? "float4(0.0f, 0.0f, 0.0f, 0.0f)" : "float4(0.0f, 0.0f, 0.0f, 1.0f)";
	case SHADER_INPUT_1:
		return with_alpha || !inputs_have_alpha ? "input1" : "float4(input1.rgb, 1.0f)";
	case SHADER_INPUT_2:
		return with_alpha || !inputs_have_alpha ? "input2" : "float4(input2.rgb, 1.0f)";
	case SHADER_INPUT_3:
		return with_alpha || !inputs_have_alpha ? "input3" : "float4(input3.rgb, 1.0f)";
	case SHADER_INPUT_4:
		return with_alpha || !inputs_have_alpha ? "input4" : "float4(input4.rgb, 1.0f)";
	case SHADER_TEXEL0:
		return with_alpha ? "texVal0" : "float4(texVal0.rgb, 1.0f)";
	case SHADER_TEXEL0A:
		if (hint_single_element) {
			return "float4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)";
		}
		else {
			if (with_alpha) {
				return "float4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)";
			}
			else {
				return "float4(texVal0.a, texVal0.a, texVal0.a, 1.0f)";
			}
		}
	case SHADER_TEXEL1:
		return with_alpha ? "texVal1" : "float4(texVal1.rgb, 1.0f)";
	}
}

std::string colorFormula(int c[2][4], int do_single, int do_multiply, int do_mix, bool with_alpha, int opt_alpha) {
	if (do_single) {
		return colorInput(c[0][3], with_alpha, opt_alpha, false);
	}
	else if (do_multiply) {
		return colorInput(c[0][0], with_alpha, opt_alpha, false) + " * " + colorInput(c[0][2], with_alpha, opt_alpha, true);
	}
	else if (do_mix) {
		return "lerp(" + colorInput(c[0][1], with_alpha, opt_alpha, false) + ", " + colorInput(c[0][0], with_alpha, opt_alpha, false) + ", " + colorInput(c[0][2], with_alpha, opt_alpha, true) + ")";
	}
	else {
		return "(" + colorInput(c[0][0], with_alpha, opt_alpha, false) + " - " + colorInput(c[0][1], with_alpha, opt_alpha, false) + ") * " + colorInput(c[0][2], with_alpha, opt_alpha, true) + ".r + " + colorInput(c[0][3], with_alpha, opt_alpha, false);
	}
}

std::string alphaInput(int item) {
	switch (item) {
	default:
	case SHADER_0:
		return "0.0f";
	case SHADER_INPUT_1:
		return "input1.a";
	case SHADER_INPUT_2:
		return "input2.a";
	case SHADER_INPUT_3:
		return "input3.a";
	case SHADER_INPUT_4:
		return "input4.a";
	case SHADER_TEXEL0:
		return "texVal0.a";
	case SHADER_TEXEL0A:
		return "texVal0.a";
	case SHADER_TEXEL1:
		return "texVal1.a";
	}
}

std::string alphaFormula(int c[2][4], int do_single, int do_multiply, int do_mix, bool with_alpha, int opt_alpha) {
	if (do_single) {
		return alphaInput(c[1][3]);
	}
	else if (do_multiply) {
		return alphaInput(c[1][0]) + " * " + alphaInput(c[1][2]);
	}
	else if (do_mix) {
		return "lerp(" + alphaInput(c[1][1]) + ", " + alphaInput(c[1][0]) + ", " + alphaInput(c[1][2]) + ")";
	}
	else {
		return "(" + alphaInput(c[1][0]) + " - " + alphaInput(c[1][1]) + ") * " + alphaInput(c[1][2]) + " + " + alphaInput(c[1][3]);
	}
}

D3D12_FILTER toD3DTexFilter(RT64::Shader::Filter filter) {
	switch (filter) {
	case RT64::Shader::Filter::Linear:
		return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	default:
	case RT64::Shader::Filter::Point:
		return D3D12_FILTER_MIN_MAG_MIP_POINT;
	}
}

D3D12_TEXTURE_ADDRESS_MODE toD3DTexAddr(RT64::Shader::AddressingMode addr) {
	switch (addr) {
	case RT64::Shader::AddressingMode::Mirror:
		return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	case RT64::Shader::AddressingMode::Clamp:
		return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	default:
	case RT64::Shader::AddressingMode::Wrap:
		return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}

#define SHADER_OPT_ALPHA (1 << 24)
#define SHADER_OPT_FOG (1 << 25)
#define SHADER_OPT_TEXTURE_EDGE (1 << 26)
#define SHADER_OPT_NOISE (1 << 27)

void RT64::Shader::generateSurfaceHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName) {
	int c[2][4];
	for (int i = 0; i < 4; i++) {
		c[0][i] = (shaderId >> (i * 3)) & 7;
		c[1][i] = (shaderId >> (12 + i * 3)) & 7;
	}

	int inputCount = 0;
	bool useTextures[2] = { false, false };
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 4; j++) {
			if (c[i][j] >= SHADER_INPUT_1 && c[i][j] <= SHADER_INPUT_4) {
				if (c[i][j] > inputCount) {
					inputCount = c[i][j];
				}
			}
			if (c[i][j] == SHADER_TEXEL0 || c[i][j] == SHADER_TEXEL0A) {
				useTextures[0] = true;
			}
			if (c[i][j] == SHADER_TEXEL1) {
				useTextures[1] = true;
			}
		}
	}

	int do_single[2];
	int do_multiply[2];
	int do_mix[2];
	int color_alpha_same;
	int opt_alpha;
	int opt_fog;
	int opt_texture_edge;
	int opt_noise;
	do_single[0] = c[0][2] == 0;
	do_single[1] = c[1][2] == 0;
	do_multiply[0] = c[0][1] == 0 && c[0][3] == 0;
	do_multiply[1] = c[1][1] == 0 && c[1][3] == 0;
	do_mix[0] = c[0][1] == c[0][3];
	do_mix[1] = c[1][1] == c[1][3];

	color_alpha_same = (shaderId & 0xfff) == ((shaderId >> 12) & 0xfff);
	opt_alpha = (shaderId & SHADER_OPT_ALPHA) != 0;
	opt_fog = (shaderId & SHADER_OPT_FOG) != 0;
	opt_texture_edge = (shaderId & SHADER_OPT_TEXTURE_EDGE) != 0;
	opt_noise = (shaderId & SHADER_OPT_NOISE) != 0;

	std::stringstream ss;
	incMeshBuffers(ss);
	incMeshFunctions(ss);
	incRayAttributes(ss);
	incGlobalHitBuffers(ss);
	incInstanceBuffers(ss);

	unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
	if (useTextures[0]) {
		SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
		incTextures(ss);
	}

	SS("struct HitInfo {");
	SS("    uint nhits;");
	SS("    uint ohits;");
	SS("};");

	SS("[shader(\"anyhit\")]");
	SS("void " << anyHitName << "(inout HitInfo payload, Attributes attrib) {");
	SS("    uint instanceId = NonUniformResourceIndex(InstanceIndex());");
	SS("    uint triangleIndex = PrimitiveIndex();");
	SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");

	getVertexData(ss, true, true, useTextures[0], inputCount);

	if (useTextures[0]) {
		SS("    int diffuseTexIndex = instanceMaterials[instanceId].materialProperties.diffuseTexIndex;");
		SS("    float4 texVal0 = gTextures[diffuseTexIndex].SampleLevel(gTextureSampler, vertexUV, 0);");
	}

	if (useTextures[1]) {
		// TODO
		SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
	}

	if (!color_alpha_same && opt_alpha) {
		SS("    float4 resultColor = float4(" + colorFormula(c, do_single[0], do_multiply[0], do_mix[0], false, true) + ".rgb, " + alphaFormula(c, do_single[1], do_multiply[1], do_mix[1], true, true) + ");");
	}
	else {
		SS("    float4 resultColor = " + colorFormula(c, do_single[0], do_multiply[0], do_mix[0], opt_alpha, opt_alpha) + ";");
	}

	SS("    uint2 pixelIdx = DispatchRaysIndex().xy;");
	SS("    uint2 pixelDims = DispatchRaysDimensions().xy;");
	SS("    uint hitStride = pixelDims.x * pixelDims.y;");
	SS("    float tval = RayTCurrent();");
	SS("    uint hi = getHitBufferIndex(min(payload.nhits, MAX_HIT_QUERIES), pixelIdx, pixelDims);");
	SS("    uint minHi = getHitBufferIndex(payload.ohits, pixelIdx, pixelDims);");
	SS("    uint lo = hi - hitStride;");
	SS("    while ((hi > minHi) && (tval < gHitDistance[lo])) {");
	SS("        gHitDistance[hi] = gHitDistance[lo];");
	SS("        gHitColor[hi] = gHitColor[lo];");
	SS("        gHitNormal[hi] = gHitNormal[lo];");
	SS("        gHitSpecular[hi] = gHitSpecular[lo];");
	SS("        gHitInstanceId[hi] = gHitInstanceId[lo];");
	SS("        hi -= hitStride;");
	SS("        lo -= hitStride;");
	SS("    }");
	SS("    uint hitPos = hi / hitStride;");
	SS("    if (hitPos < MAX_HIT_QUERIES) {");
	SS("        gHitDistance[hi] = tval;");
	SS("        gHitColor[hi] = resultColor;");
	SS("        gHitNormal[hi] = float4(vertexNormal, 1.0f);");
	SS("        gHitSpecular[hi] = 0.0f;");
	SS("        gHitInstanceId[hi] = instanceId;");
	SS("        ++payload.nhits;");
	SS("        if (hitPos != MAX_HIT_QUERIES - 1) {");
	SS("            IgnoreHit();");
	SS("        }");
	SS("    }");
	SS("    else {");
	SS("        IgnoreHit();");
	SS("    }");
	SS("}");
	SS("[shader(\"closesthit\")]");
	SS("void " << closestHitName << "(inout HitInfo payload, Attributes attrib) { }");

	// Compile shader.
	std::string shaderCode = ss.str();
#ifndef NDEBUG
	fprintf(stdout, "0x%X: %s\n", shaderId, shaderCode.c_str());
#endif
	compileShaderCode(shaderCode, &surfaceHitGroup.shaderBlob);

	{
		// Generate root signature.
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, SRV_INDEX(vertexBuffer));
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, SRV_INDEX(indexBuffer));
		rsc.AddHeapRangesParameter({
			{ UAV_INDEX(gHitDistance), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitDistance) },
			{ UAV_INDEX(gHitColor), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitColor) },
			{ UAV_INDEX(gHitNormal), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitNormal) },
			{ UAV_INDEX(gHitSpecular), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitSpecular) },
			{ UAV_INDEX(gHitInstanceId), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitInstanceId) },
			{ SRV_INDEX(instanceTransforms), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceTransforms) },
			{ SRV_INDEX(instanceMaterials), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceMaterials) },
			{ SRV_INDEX(gTextures), 1024, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(gTextures) },
			//{ CBV_INDEX(ViewParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, HEAP_INDEX(ViewParams) }
		});

		// Generate the correct type of sampler.
		D3D12_STATIC_SAMPLER_DESC samplerDesc = { };
		samplerDesc.Filter = toD3DTexFilter(filter);
		samplerDesc.AddressU = toD3DTexAddr(hAddr);
		samplerDesc.AddressV = toD3DTexAddr(vAddr);
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplerDesc.ShaderRegister = samplerRegisterIndex;
		samplerDesc.RegisterSpace = 0;

		surfaceHitGroup.rootSignature = rsc.Generate(device->getD3D12Device(), true, false, &samplerDesc, 1);
	}

	// Store names.
	surfaceHitGroup.hitGroupName = win32::Utf8ToUtf16(hitGroupName);
	surfaceHitGroup.closestHitName = win32::Utf8ToUtf16(closestHitName);
	surfaceHitGroup.anyHitName = win32::Utf8ToUtf16(anyHitName);
}

void RT64::Shader::generateShadowHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName) {
	int c[2][4];
	for (int i = 0; i < 4; i++) {
		c[0][i] = (shaderId >> (i * 3)) & 7;
		c[1][i] = (shaderId >> (12 + i * 3)) & 7;
	}

	int inputCount = 0;
	bool useTextures[2] = { false, false };
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 4; j++) {
			if (c[i][j] >= SHADER_INPUT_1 && c[i][j] <= SHADER_INPUT_4) {
				if (c[i][j] > inputCount) {
					inputCount = c[i][j];
				}
			}
			if (c[i][j] == SHADER_TEXEL0 || c[i][j] == SHADER_TEXEL0A) {
				useTextures[0] = true;
			}
			if (c[i][j] == SHADER_TEXEL1) {
				useTextures[1] = true;
			}
		}
	}

	int do_single[2];
	int do_multiply[2];
	int do_mix[2];
	int color_alpha_same;
	int opt_alpha;
	int opt_fog;
	int opt_texture_edge;
	int opt_noise;
	do_single[0] = c[0][2] == 0;
	do_single[1] = c[1][2] == 0;
	do_multiply[0] = c[0][1] == 0 && c[0][3] == 0;
	do_multiply[1] = c[1][1] == 0 && c[1][3] == 0;
	do_mix[0] = c[0][1] == c[0][3];
	do_mix[1] = c[1][1] == c[1][3];

	color_alpha_same = (shaderId & 0xfff) == ((shaderId >> 12) & 0xfff);
	opt_alpha = (shaderId & SHADER_OPT_ALPHA) != 0;
	opt_fog = (shaderId & SHADER_OPT_FOG) != 0;
	opt_texture_edge = (shaderId & SHADER_OPT_TEXTURE_EDGE) != 0;
	opt_noise = (shaderId & SHADER_OPT_NOISE) != 0;

	std::stringstream ss;
	incMeshBuffers(ss);
	incMeshFunctions(ss);
	incRayAttributes(ss);
	incInstanceBuffers(ss);

	unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
	if (useTextures[0]) {
		SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
		incTextures(ss);
	}

	SS("struct ShadowHitInfo {");
	SS("    float shadowHit;");
	SS("};");

	SS("[shader(\"anyhit\")]");
	SS("void " << anyHitName << "(inout ShadowHitInfo payload, Attributes attrib) {");
	if (opt_alpha) {
		SS("    uint instanceId = NonUniformResourceIndex(InstanceIndex());");
		SS("    uint triangleIndex = PrimitiveIndex();");
		SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");

		getVertexData(ss, false, false, useTextures[0], inputCount);

		if (useTextures[0]) {
			SS("    int diffuseTexIndex = instanceMaterials[instanceId].materialProperties.diffuseTexIndex;");
			SS("    float4 texVal0 = gTextures[diffuseTexIndex].SampleLevel(gTextureSampler, vertexUV, 0);");
		}

		if (useTextures[1]) {
			// TODO
			SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
		}

		if (!color_alpha_same && opt_alpha) {
			SS("    float resultAlpha = " + alphaFormula(c, do_single[1], do_multiply[1], do_mix[1], true, true) + ";");
		}
		else {
			SS("    float resultAlpha = " + colorFormula(c, do_single[0], do_multiply[0], do_mix[0], opt_alpha, opt_alpha) + ".a;");
		}

		SS("    resultAlpha = clamp(resultAlpha * instanceMaterials[instanceId].materialProperties.shadowAlphaMultiplier, 0.0f, 1.0f);");
		SS("    payload.shadowHit = max(payload.shadowHit - resultAlpha, 0.0f);");
		SS("    if (payload.shadowHit > 0.0f) {");
		SS("		IgnoreHit();");
		SS("    }");
	}
	else {
		SS("payload.shadowHit = 0.0f;");
	}
	SS("}");
	SS("[shader(\"closesthit\")]");
	SS("void " << closestHitName << "(inout ShadowHitInfo payload, Attributes attrib) { }");

	// Compile shader.
	std::string shaderCode = ss.str();
#ifndef NDEBUG
	fprintf(stdout, "0x%X: %s\n", shaderId, shaderCode.c_str());
#endif
	compileShaderCode(shaderCode, &shadowHitGroup.shaderBlob);

	{
		// Generate root signature.
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, SRV_INDEX(vertexBuffer));
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, SRV_INDEX(indexBuffer));
		rsc.AddHeapRangesParameter({
			{ SRV_INDEX(instanceTransforms), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceTransforms) },
			{ SRV_INDEX(instanceMaterials), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceMaterials) },
			{ SRV_INDEX(gTextures), 1024, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(gTextures) },
			//{ CBV_INDEX(ViewParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, HEAP_INDEX(ViewParams) }
		});

		// Generate the correct type of sampler.
		D3D12_STATIC_SAMPLER_DESC samplerDesc = { };
		samplerDesc.Filter = toD3DTexFilter(filter);
		samplerDesc.AddressU = toD3DTexAddr(hAddr);
		samplerDesc.AddressV = toD3DTexAddr(vAddr);
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplerDesc.ShaderRegister = samplerRegisterIndex;
		samplerDesc.RegisterSpace = 0;

		shadowHitGroup.rootSignature = rsc.Generate(device->getD3D12Device(), true, false, &samplerDesc, 1);
	}

	// Store names.
	shadowHitGroup.hitGroupName = win32::Utf8ToUtf16(hitGroupName);
	shadowHitGroup.closestHitName = win32::Utf8ToUtf16(closestHitName);
	shadowHitGroup.anyHitName = win32::Utf8ToUtf16(anyHitName);
}

void RT64::Shader::compileShaderCode(const std::string &shaderCode, IDxcBlob **shaderBlob) {
    IDxcBlobEncoding *textBlob = nullptr;
    D3D12_CHECK(device->getDxcLibrary()->CreateBlobWithEncodingFromPinned((LPBYTE)shaderCode.c_str(), (uint32_t)shaderCode.size(), 0, &textBlob));

	std::vector<LPCWSTR> arguments;
	arguments.push_back(L"-Qstrip_debug");
	arguments.push_back(L"-Qstrip_reflect");

    IDxcOperationResult *result = nullptr;
    D3D12_CHECK(device->getDxcCompiler()->Compile(textBlob, L"", L"", L"lib_6_3", arguments.data(), arguments.size(), nullptr, 0, nullptr, &result));

    HRESULT resultCode;
    D3D12_CHECK(result->GetStatus(&resultCode));
    if (FAILED(resultCode)) {
        IDxcBlobEncoding *error;
        HRESULT hr = result->GetErrorBuffer(&error);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to get shader compiler error");
        }

        // Convert error blob to a string.
        std::vector<char> infoLog(error->GetBufferSize() + 1);
        memcpy(infoLog.data(), error->GetBufferPointer(), error->GetBufferSize());
        infoLog[error->GetBufferSize()] = 0;

        throw std::runtime_error("Shader compilation error: " + std::string(infoLog.data()));
    }

    D3D12_CHECK(result->GetResult(shaderBlob));
}

RT64::Shader::HitGroup &RT64::Shader::getSurfaceHitGroup() {
	return surfaceHitGroup;
}

RT64::Shader::HitGroup &RT64::Shader::getShadowHitGroup() {
	return shadowHitGroup;
}

// Public

RT64::Shader::Filter convertFilter(unsigned int filter) {
	switch (filter) {
	case RT64_SHADER_FILTER_LINEAR:
		return RT64::Shader::Filter::Linear;
	case RT64_SHADER_FILTER_POINT:
	default:
		return RT64::Shader::Filter::Point;
	}
}

RT64::Shader::AddressingMode convertAddressingMode(unsigned int mode) {
	switch (mode) {
	case RT64_SHADER_ADDRESSING_CLAMP:
		return RT64::Shader::AddressingMode::Clamp;
	case RT64_SHADER_ADDRESSING_MIRROR:
		return RT64::Shader::AddressingMode::Mirror;
	case RT64_SHADER_ADDRESSING_WRAP:
	default:
		return RT64::Shader::AddressingMode::Wrap;
	}
}

DLLEXPORT RT64_SHADER *RT64_CreateShader(RT64_DEVICE *devicePtr, unsigned int shaderId, unsigned int filter, unsigned int hAddr, unsigned int vAddr) {
    try {
        RT64::Device *device = (RT64::Device *)(devicePtr);
		RT64::Shader::Filter sFilter = convertFilter(filter);
		RT64::Shader::AddressingMode sHAddr = convertAddressingMode(hAddr);
		RT64::Shader::AddressingMode sVAddr = convertAddressingMode(vAddr);
        return (RT64_SHADER *)(new RT64::Shader(device, shaderId, sFilter, sHAddr, sVAddr));
    }
    RT64_CATCH_EXCEPTION();
    return nullptr;
}

DLLEXPORT void RT64_DestroyShader(RT64_SHADER *shaderPtr) {
	delete (RT64::Shader *)(shaderPtr);
}

#endif

/*
//
// RT64
//

#include "GlobalHitBuffers.hlsli"
#include "Instances.hlsli"
#include "Mesh.hlsli"
#include "Ray.hlsli"
#include "Samplers.hlsli"
#include "Textures.hlsli"
#include "ViewParams.hlsli"

[shader("anyhit")]
void SurfaceAnyHit(inout HitInfo payload, Attributes attrib) {
	// Sample texture color and execute color combiner.
	uint instanceId = NonUniformResourceIndex(InstanceIndex());
	uint triangleId = PrimitiveIndex();
	int diffuseTexIndex = instanceMaterials[instanceId].materialProperties.diffuseTexIndex;
	float4 diffuseColorMix = instanceMaterials[instanceId].materialProperties.diffuseColorMix;
	float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);
	VertexAttributes vertex = GetVertexAttributes(vertexBuffer, indexBuffer, triangleId, barycentrics);
	float4 texelColor = SampleTexture(gTextures[diffuseTexIndex], vertex.uv, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode);

	// Only mix the texture if the alpha value is negative.
	texelColor.rgb = lerp(texelColor.rgb, diffuseColorMix.rgb, max(-diffuseColorMix.a, 0.0f));

	ColorCombinerInputs ccInputs;
	ccInputs.input1 = vertex.input[0];
	ccInputs.input2 = vertex.input[1];
	ccInputs.input3 = vertex.input[2];
	ccInputs.input4 = vertex.input[3];
	ccInputs.texVal0 = texelColor;
	ccInputs.texVal1 = texelColor;

	uint noiseScale = resolution.y / NOISE_SCALE_HEIGHT;
	uint seed = initRand((DispatchRaysIndex().x / noiseScale) + (DispatchRaysIndex().y / noiseScale) * DispatchRaysDimensions().x, frameCount, 16);
	float4 resultColor = CombineColors(instanceMaterials[instanceId].ccFeatures, ccInputs, seed);
	resultColor.a = clamp(instanceMaterials[instanceId].materialProperties.solidAlphaMultiplier * resultColor.a, 0.0f, 1.0f);

	// Ignore hit if alpha is empty.
	static const float Epsilon = 0.00001f;
	if (resultColor.a > Epsilon) {
		// Get best index to store the data on.
		uint2 pixelIdx = DispatchRaysIndex().xy;
		uint2 pixelDims = DispatchRaysDimensions().xy;
		uint hitStride = pixelDims.x * pixelDims.y;

		// HACK: Add some bias for the comparison based on the instance ID so coplanar surfaces are friendlier with each other.
		// This can likely be implemented as an instance property at some point to control depth sorting.
		float tval = WithDistanceBias(RayTCurrent(), instanceId);
		uint hi = getHitBufferIndex(min(payload.nhits, MAX_HIT_QUERIES), pixelIdx, pixelDims);
		uint minHi = getHitBufferIndex(payload.ohits, pixelIdx, pixelDims);
		uint lo = hi - hitStride;
		while ((hi > minHi) && (tval < gHitDistance[lo]))
		{
			gHitDistance[hi] = gHitDistance[lo];
			gHitColor[hi] = gHitColor[lo];
			gHitNormal[hi] = gHitNormal[lo];
			gHitSpecular[hi] = gHitSpecular[lo];
			gHitInstanceId[hi] = gHitInstanceId[lo];
			hi -= hitStride;
			lo -= hitStride;
		}

		uint hitPos = hi / hitStride;
		if (hitPos < MAX_HIT_QUERIES) {
			// Only mix the final diffuse color if the alpha is positive.
			resultColor.rgb = lerp(resultColor.rgb, diffuseColorMix.rgb, max(diffuseColorMix.a, 0.0f));

			int normalTexIndex = instanceMaterials[instanceId].materialProperties.normalTexIndex;
			if (normalTexIndex >= 0) {
				float uvDetailScale = instanceMaterials[instanceId].materialProperties.uvDetailScale;
				float3 normalColor = SampleTexture(gTextures[normalTexIndex], vertex.uv * uvDetailScale, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode).xyz;
				normalColor = (normalColor * 2.0f) - 1.0f;

				float3 newNormal = normalize(vertex.normal * normalColor.z + vertex.tangent * normalColor.x + vertex.binormal * normalColor.y);
				vertex.normal = newNormal;
			}

			vertex.normal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertex.normal, 0.f)).xyz);

			// Flip the normal if this is hitting the backface.
			vertex.triNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertex.triNormal, 0.f)).xyz);
			bool isBackFacing = dot(vertex.triNormal, WorldRayDirection()) > 0.0f;
			if (isBackFacing) {
				vertex.normal = -vertex.normal;
			}

			// Sample the specular map.
			float specularColor = 1.0f;
			int specularTexIndex = instanceMaterials[instanceId].materialProperties.specularTexIndex;
			if (specularTexIndex >= 0) {
				float uvDetailScale = instanceMaterials[instanceId].materialProperties.uvDetailScale;
				specularColor = SampleTexture(gTextures[specularTexIndex], vertex.uv * uvDetailScale, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode).r;
			}

			// Store hit data and increment the hit counter.
			gHitDistance[hi] = tval;
			gHitColor[hi] = resultColor;
			gHitNormal[hi] = float4(vertex.normal, 1.0f);
			gHitSpecular[hi] = specularColor;
			gHitInstanceId[hi] = instanceId;

			++payload.nhits;

			if (hitPos != MAX_HIT_QUERIES - 1) {
				IgnoreHit();
			}
		}
		else {
			IgnoreHit();
		}
	}
	else {
		IgnoreHit();
	}
}

[shader("closesthit")]
void SurfaceClosestHit(inout HitInfo payload, Attributes attrib) {
	// No-op.
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}
*/

/*
//
// RT64
//

#include "Instances.hlsli"
#include "Mesh.hlsli"
#include "Ray.hlsli"
#include "Samplers.hlsli"
#include "Textures.hlsli"
#include "ViewParams.hlsli"

[shader("anyhit")]
void ShadowAnyHit(inout ShadowHitInfo payload, Attributes attrib) {
	uint instanceId = NonUniformResourceIndex(InstanceIndex());
	if (instanceMaterials[instanceId].ccFeatures.opt_alpha) {
		uint triangleId = PrimitiveIndex();
		float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);
		VertexAttributes vertex = GetVertexAttributes(vertexBuffer, indexBuffer, triangleId, barycentrics);
		int diffuseTexIndex = instanceMaterials[instanceId].materialProperties.diffuseTexIndex;
		float4 texelColor = SampleTexture(gTextures[diffuseTexIndex], vertex.uv, instanceMaterials[instanceId].materialProperties.filterMode, instanceMaterials[instanceId].materialProperties.hAddressMode, instanceMaterials[instanceId].materialProperties.vAddressMode);

		ColorCombinerInputs ccInputs;
		ccInputs.input1 = vertex.input[0];
		ccInputs.input2 = vertex.input[1];
		ccInputs.input3 = vertex.input[2];
		ccInputs.input4 = vertex.input[3];
		ccInputs.texVal0 = texelColor;
		ccInputs.texVal1 = texelColor;

		uint noiseScale = resolution.y / NOISE_SCALE_HEIGHT;
		uint seed = initRand((DispatchRaysIndex().x / noiseScale) + (DispatchRaysIndex().y / noiseScale) * DispatchRaysDimensions().x, frameCount, 16);
		float resultAlpha = clamp(CombineColors(instanceMaterials[instanceId].ccFeatures, ccInputs, seed).a * instanceMaterials[instanceId].materialProperties.shadowAlphaMultiplier, 0.0f, 1.0f);
		payload.shadowHit = max(payload.shadowHit - resultAlpha, 0.0f);
		if (payload.shadowHit > 0.0f) {
			IgnoreHit();
		}
	}
	else {
		payload.shadowHit = 0.0f;
	}
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowHitInfo payload, Attributes attrib) {
	// No-op.
}

[shader("miss")]
void SurfaceMiss(inout HitInfo payload : SV_RayPayload) {
	// No-op.
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
	// No-op.
}
*/

/*
// Compute the tangent vector for the polygon.
	// Derived from http://area.autodesk.com/blogs/the-3ds-max-blog/how_the_3ds_max_scanline_renderer_computes_tangent_and_binormal_vectors_for_normal_mapping
	// TODO: Evaluate how to accomodate this to the smoothed normal.
	// TODO: Only do this if it actually uses a normal map. Likely need specialized shader generation to solve this properly.
{
	float uva = uv1.x - uv0.x;
	float uvb = uv2.x - uv0.x;
	float uvc = uv1.y - uv0.y;
	float uvd = uv2.y - uv0.y;
	float uvk = uvb * uvc - uva * uvd;
	float3 dpos1 = pos1 - pos0;
	float3 dpos2 = pos2 - pos0;
	if (uvk != 0) {
		v.tangent = normalize((uvc * dpos2 - uvd * dpos1) / uvk);
	}
	else {
		if (uva != 0) {
			v.tangent = normalize(dpos1 / uva);
		}
		else if (uvb != 0) {
			v.tangent = normalize(dpos2 / uvb);
		}
		else {
			v.tangent = 0.0f;
		}
	}

	float2 duv1 = uv1 - uv0;
	float2 duv2 = uv2 - uv1;
	duv1.y = -duv1.y;
	duv2.y = -duv2.y;
	float3 cr = cross(float3(duv1.xy, 0.0f), float3(duv2.xy, 0.0f));
	float binormalMult = (cr.z < 0.0f) ? -1.0f : 1.0f;
	v.binormal = cross(v.tangent, v.normal) * binormalMult;
}
*/