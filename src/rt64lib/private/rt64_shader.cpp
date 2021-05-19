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
    generateShader(shaderId, filter, hAddr, vAddr, closestHit, anyHit);

	// Store symbol names.
	hitGroupName = win32::Utf8ToUtf16(hitGroup);
	closestHitName = win32::Utf8ToUtf16(closestHit);
	anyHitName = win32::Utf8ToUtf16(anyHit);
	shaderSymbols.push_back(closestHitName);
	shaderSymbols.push_back(anyHitName);

	device->addShader(this);
}

RT64::Shader::~Shader() {
	device->removeShader(this);

    shaderBlob->Release();
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

void incSurfaceRayStructs(std::stringstream &ss) {
	SS("struct HitInfo {");
	SS("    uint nhits;");
	SS("    uint ohits;");
	SS("};");
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

void getVertexData(std::stringstream &ss, bool vertexUV) {
	SS("uint3 index3 = GetIndices(indexBuffer, triangleIndex);");
	SS("float3 pos0 = asfloat(vertexBuffer.Load3(PosAddr(index3[0])));");
	SS("float3 pos1 = asfloat(vertexBuffer.Load3(PosAddr(index3[1])));");
	SS("float3 pos2 = asfloat(vertexBuffer.Load3(PosAddr(index3[2])));");
	SS("float3 norm0 = asfloat(vertexBuffer.Load3(NormAddr(index3[0])));");
	SS("float3 norm1 = asfloat(vertexBuffer.Load3(NormAddr(index3[1])));");
	SS("float3 norm2 = asfloat(vertexBuffer.Load3(NormAddr(index3[2])));");
	SS("float3 vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];");
	SS("float3 vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];");
	SS("float3 triangleNormal = -cross(pos2 - pos0, pos1 - pos0);");
	SS("vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : triangleNormal;");
	SS("vertexNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexNormal, 0.f)).xyz);");
	SS("triangleNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(triangleNormal, 0.f)).xyz);");
	SS("bool isBackFacing = dot(triangleNormal, WorldRayDirection()) > 0.0f;");
	SS("if (isBackFacing) { vertexNormal = -vertexNormal; }");

	if (vertexUV) {
		SS("float2 uv0 = asfloat(vertexBuffer.Load2(UvAddr(index3[0])));");
		SS("float2 uv1 = asfloat(vertexBuffer.Load2(UvAddr(index3[1])));");
		SS("float2 uv2 = asfloat(vertexBuffer.Load2(UvAddr(index3[2])));");
		SS("float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];");
	}
}

void incTextures(std::stringstream &ss) {
	SS("Texture2D<float4> gTextures[1024] : register(t7);");
}

enum {
	CC_0,
	CC_TEXEL0,
	CC_TEXEL1,
	CC_PRIM,
	CC_SHADE,
	CC_ENV,
	CC_TEXEL0A,
	CC_LOD
};

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

#define SHADER_OPT_ALPHA (1 << 24)
#define SHADER_OPT_FOG (1 << 25)
#define SHADER_OPT_TEXTURE_EDGE (1 << 26)
#define SHADER_OPT_NOISE (1 << 27)

void RT64::Shader::generateShader(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &closestHitName, const std::string &anyHitName) {
	/// Parsing shader id
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

	///
	/*
	// N64 Color Combiner.
	uint32_t shader_id = prg->shader_id;
	mat.c0[0] = (shader_id >> (0 * 3)) & 7;
	mat.c0[1] = (shader_id >> (1 * 3)) & 7;
	mat.c0[2] = (shader_id >> (2 * 3)) & 7;
	mat.c0[3] = (shader_id >> (3 * 3)) & 7;
	mat.c1[0] = (shader_id >> (12 + 0 * 3)) & 7;
	mat.c1[1] = (shader_id >> (12 + 1 * 3)) & 7;
	mat.c1[2] = (shader_id >> (12 + 2 * 3)) & 7;
	mat.c1[3] = (shader_id >> (12 + 3 * 3)) & 7;
	mat.do_single[0] = mat.c0[2] == 0;
	mat.do_single[1] = mat.c1[2] == 0;
	mat.do_multiply[0] = mat.c0[1] == 0 && mat.c0[3] == 0;
	mat.do_multiply[1] = mat.c1[1] == 0 && mat.c1[3] == 0;
	mat.do_mix[0] = mat.c0[1] == mat.c0[3];
	mat.do_mix[1] = mat.c1[1] == mat.c1[3];
	mat.color_alpha_same = (shader_id & 0xfff) == ((shader_id >> 12) & 0xfff);
	mat.opt_alpha = (shader_id & SHADER_OPT_ALPHA) != 0;
	mat.opt_fog = (shader_id & SHADER_OPT_FOG) != 0;
	mat.opt_texture_edge = (shader_id & SHADER_OPT_TEXTURE_EDGE) != 0;
	mat.opt_noise = (shader_id & SHADER_OPT_NOISE) != 0;
	*/

    std::stringstream ss;
	incMeshBuffers(ss);
	incMeshFunctions(ss);
	incSurfaceRayStructs(ss);
	incGlobalHitBuffers(ss);
	incInstanceBuffers(ss);

	unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
	if (useTextures[0]) {
		SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
		incTextures(ss);
	}

	SS("[shader(\"anyhit\")]");
	SS("void " << anyHitName << "(inout HitInfo payload, Attributes attrib) {");
	SS("    uint instanceId = NonUniformResourceIndex(InstanceIndex());");
	SS("    uint triangleIndex = PrimitiveIndex();");
	SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");

	getVertexData(ss, useTextures[0]);

	SS("    float4 resultColor = float4(0.5f, 0.5f, 0.5f, 1.0f);");

	if (useTextures[0]) {
		SS("    int diffuseTexIndex = instanceMaterials[instanceId].materialProperties.diffuseTexIndex;");
		SS("    float4 texelColor = gTextures[diffuseTexIndex].SampleLevel(gTextureSampler, vertexUV, 0);");
		///
		SS("    resultColor = texelColor;");
		///
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


	std::string shaderCode = ss.str();

#ifndef NDEBUG
	fprintf(stdout, "0x%X: %s\n", shaderId, shaderCode.c_str());
#endif

	compileShaderCode(shaderCode);
	generateRootSignature(filter, hAddr, vAddr, samplerRegisterIndex);
}

void RT64::Shader::compileShaderCode(const std::string &shaderCode) {
    IDxcBlobEncoding *textBlob = nullptr;
    D3D12_CHECK(device->getDxcLibrary()->CreateBlobWithEncodingFromPinned((LPBYTE)shaderCode.c_str(), (uint32_t)shaderCode.size(), 0, &textBlob));

    IDxcOperationResult *result = nullptr;
    D3D12_CHECK(device->getDxcCompiler()->Compile(textBlob, L"", L"", L"lib_6_3", nullptr, 0, nullptr, 0, nullptr, &result));

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

    D3D12_CHECK(result->GetResult(&shaderBlob));
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

void RT64::Shader::generateRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex) {
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

	rootSignature = rsc.Generate(device->getD3D12Device(), true, false, &samplerDesc, 1);
}

IDxcBlob *RT64::Shader::getBlob() const {
	return shaderBlob;
}

const std::vector<std::wstring> &RT64::Shader::getSymbolExports() const {
	return shaderSymbols;
}

const std::wstring &RT64::Shader::getHitGroupName() const {
	return hitGroupName;
}

const std::wstring &RT64::Shader::getClosestHitName() const {
	return closestHitName;
}

const std::wstring &RT64::Shader::getAnyHitName() const {
	return anyHitName;
}

ID3D12RootSignature *RT64::Shader::getRootSignature() const {
	return rootSignature;
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