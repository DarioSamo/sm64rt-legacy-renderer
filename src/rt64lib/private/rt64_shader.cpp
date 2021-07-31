//
// RT64
//
#ifndef RT64_MINIMAL

#include "rt64_shader.h"

#include "rt64_device.h"
#include "rt64_shader_hlsli.h"

#include "utf8conv/utf8conv.h"

// Private

#define TEXTURE_EDGE_ENABLED

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
#define SHADER_OPT_TEXTURE_EDGE (1 << 26)
#define SHADER_OPT_NOISE (1 << 27)

struct ColorCombinerParams {
	int c[2][4];
	int inputCount = 0;
	bool useTextures[2] = { false, false };
	int do_single[2];
	int do_multiply[2];
	int do_mix[2];
	int color_alpha_same;
	int opt_alpha;
	int opt_texture_edge;
	int opt_noise;

	ColorCombinerParams(int shaderId) {
		for (int i = 0; i < 4; i++) {
			c[0][i] = (shaderId >> (i * 3)) & 7;
			c[1][i] = (shaderId >> (12 + i * 3)) & 7;
		}

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

		do_single[0] = c[0][2] == 0;
		do_single[1] = c[1][2] == 0;
		do_multiply[0] = c[0][1] == 0 && c[0][3] == 0;
		do_multiply[1] = c[1][1] == 0 && c[1][3] == 0;
		do_mix[0] = c[0][1] == c[0][3];
		do_mix[1] = c[1][1] == c[1][3];

		color_alpha_same = (shaderId & 0xfff) == ((shaderId >> 12) & 0xfff);
		opt_alpha = (shaderId & SHADER_OPT_ALPHA) != 0;
		opt_texture_edge = (shaderId & SHADER_OPT_TEXTURE_EDGE) != 0;
		opt_noise = (shaderId & SHADER_OPT_NOISE) != 0;
	}
};

struct VertexLayout {
	int vertexSize = 0;
	int positionOffset = 0;
	int normalOffset = 0;
	int uvOffset = 0;
	int inputOffset[4] = { 0,0,0,0 };

	VertexLayout(bool vertexPosition, bool vertexNormal, bool vertexUV, int inputCount, bool useAlpha) {
		positionOffset = vertexSize; if (vertexPosition) vertexSize += 16;
		normalOffset = vertexSize; if (vertexNormal) vertexSize += 12;
		uvOffset = vertexSize; if (vertexUV) vertexSize += 8;
		for (int i = 0; i < inputCount; i++) {
			inputOffset[i] = vertexSize;
			vertexSize += useAlpha ? 16 : 12;
		}
	}
};

RT64::Shader::Shader(Device *device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, int flags) {
	assert(device != nullptr);
	this->device = device;

	bool normalMapEnabled = flags & RT64_SHADER_NORMAL_MAP_ENABLED;
	bool specularMapEnabled = flags & RT64_SHADER_SPECULAR_MAP_ENABLED;
	const std::string baseName =
		"Shader_" +
		std::to_string(shaderId) +
		"_" + std::to_string(uniqueSamplerRegisterIndex(filter, hAddr, vAddr)) +
		(normalMapEnabled ? "_Nrm" : "") +
		(specularMapEnabled ? "_Spc" : "");

	if (flags & RT64_SHADER_RASTER_ENABLED) {
		const std::string vertexShader = baseName + "VS";
		const std::string pixelShader = baseName + "PS";
		generateRasterGroup(shaderId, filter, hAddr, vAddr, vertexShader, pixelShader);
	}

	if (flags & RT64_SHADER_RAYTRACE_ENABLED) {
		const std::string hitGroup = baseName + "HitGroup";
		const std::string closestHit = baseName + "ClosestHit";
		const std::string anyHit = baseName + "AnyHit";
		const std::string shadowHitGroup = baseName + "ShadowHitGroup";
		const std::string shadowClosestHit = baseName + "ShadowClosestHit";
		const std::string shadowAnyHit = baseName + "ShadowAnyHit";
		generateSurfaceHitGroup(shaderId, filter, hAddr, vAddr, normalMapEnabled, specularMapEnabled, hitGroup, closestHit, anyHit);
		generateShadowHitGroup(shaderId, filter, hAddr, vAddr, shadowHitGroup, shadowClosestHit, shadowAnyHit);
	}

	device->addShader(this);
}

RT64::Shader::~Shader() {
	device->removeShader(this);
	rasterGroup.blobPS->Release();
	rasterGroup.blobVS->Release();
	rasterGroup.pipelineState->Release();
    surfaceHitGroup.blob->Release();
	shadowHitGroup.blob->Release();
}

#define SS(x) ss << x << std::endl;

unsigned int RT64::Shader::uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr) {
	// Index 0 is reserved by the sampler used in the tracer.
	unsigned int uniqueID = 1;
	uniqueID += (unsigned int)(filter) * 9;
	uniqueID += (unsigned int)(hAddr) * 3;
	uniqueID += (unsigned int)(vAddr);
	return uniqueID;
}

void incMeshBuffers(std::stringstream &ss) {
	SS("ByteAddressBuffer vertexBuffer : register(t2);");
	SS("ByteAddressBuffer indexBuffer : register(t3);");
}

void getVertexData(std::stringstream &ss, bool vertexPosition, bool vertexNormal, bool vertexUV, int inputCount, bool useAlpha, bool vertexBinormalAndTangent) {
	VertexLayout vl(vertexPosition, vertexNormal, vertexUV, inputCount, useAlpha);

	SS("uint3 index3 = indexBuffer.Load3((triangleIndex * 3) * 4);");

	if (vertexPosition) {
		for (int i = 0; i < 3; i++) {
			SS("float3 pos" + std::to_string(i) + " = asfloat(vertexBuffer.Load3(index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.positionOffset) + "));");
		}

		SS("float3 vertexPosition = pos0 * barycentrics[0] + pos1 * barycentrics[1] + pos2 * barycentrics[2];");
	}

	if (vertexNormal) {
		for (int i = 0; i < 3; i++) {
			SS("float3 norm" + std::to_string(i) + " = asfloat(vertexBuffer.Load3(index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.normalOffset) + "));");
		}

		SS("float3 vertexNormal = norm0 * barycentrics[0] + norm1 * barycentrics[1] + norm2 * barycentrics[2];");
		SS("float3 triangleNormal = -cross(pos2 - pos0, pos1 - pos0);");
		SS("vertexNormal = any(vertexNormal) ? normalize(vertexNormal) : triangleNormal;");
	}

	if (vertexUV) {
		for (int i = 0; i < 3; i++) {
			SS("float2 uv" + std::to_string(i) + " = asfloat(vertexBuffer.Load2(index3[" + std::to_string(i) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.uvOffset) + "));");
		}

		SS("float2 vertexUV = uv0 * barycentrics[0] + uv1 * barycentrics[1] + uv2 * barycentrics[2];");
	}

	for (int i = 0; i < inputCount; i++) {
		std::string floatNum = useAlpha ? "4" : "3";
		std::string index = std::to_string(i + 1);
		for (int j = 0; j < 3; j++) {
			SS("float" + floatNum + " input" + index + std::to_string(j) + " = asfloat(vertexBuffer.Load" + floatNum + "(index3[" + std::to_string(j) + "] * " + std::to_string(vl.vertexSize) + " + " + std::to_string(vl.inputOffset[i]) + "));");
		}

		SS("float4 input" + index + " = " + (useAlpha ? "" : "float4(") + "input" + index + "0 * barycentrics[0] + input" + index + "1 * barycentrics[1] + input" + index + "2 * barycentrics[2]" + (useAlpha ? "" : ", 1.0f)") + ";");
	}

	if (vertexBinormalAndTangent) {
		// Compute the tangent vector for the polygon.
		// Derived from http://area.autodesk.com/blogs/the-3ds-max-blog/how_the_3ds_max_scanline_renderer_computes_tangent_and_binormal_vectors_for_normal_mapping
		SS("float uva = uv1.x - uv0.x;");
		SS("float uvb = uv2.x - uv0.x;");
		SS("float uvc = uv1.y - uv0.y;");
		SS("float uvd = uv2.y - uv0.y;");
		SS("float uvk = uvb * uvc - uva * uvd;");
		SS("float3 dpos1 = pos1 - pos0;");
		SS("float3 dpos2 = pos2 - pos0;");
		SS("float3 vertexTangent;");
		SS("if (uvk != 0) vertexTangent = normalize((uvc * dpos2 - uvd * dpos1) / uvk);");
		SS("else {");
		SS("    if (uva != 0) vertexTangent = normalize(dpos1 / uva);");
		SS("    else if (uvb != 0) vertexTangent = normalize(dpos2 / uvb);");
		SS("    else vertexTangent = 0.0f;");
		SS("}");
		SS("float2 duv1 = uv1 - uv0;");
		SS("float2 duv2 = uv2 - uv1;");
		SS("duv1.y = -duv1.y;");
		SS("duv2.y = -duv2.y;");
		SS("float3 cr = cross(float3(duv1.xy, 0.0f), float3(duv2.xy, 0.0f));");
		SS("float binormalMult = (cr.z < 0.0f) ? -1.0f : 1.0f;");
		SS("float3 vertexBinormal = cross(vertexTangent, vertexNormal) * binormalMult;");
	}
}

void transformVertexNormal(std::stringstream &ss) {
	SS("vertexNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(vertexNormal, 0.f)).xyz);");
	SS("triangleNormal = normalize(mul(instanceTransforms[instanceId].objectToWorldNormal, float4(triangleNormal, 0.f)).xyz);");
	SS("bool isBackFacing = dot(triangleNormal, WorldRayDirection()) > 0.0f;");
	SS("if (isBackFacing) { vertexNormal = -vertexNormal; }");
}

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

void RT64::Shader::generateRasterGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &vertexShaderName, const std::string &pixelShaderName) {
	ColorCombinerParams cc(shaderId);
	bool vertexUV = cc.useTextures[0] || cc.useTextures[1];
	VertexLayout vl(true, true, vertexUV, cc.inputCount, cc.opt_alpha);

	std::stringstream ss;
	SS(INCLUDE_HLSLI(MaterialsHLSLI));
	SS(INCLUDE_HLSLI(InstancesHLSLI));
	SS("int instanceId : register(b0);");

	unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
	if (cc.useTextures[0]) {
		SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
		SS(INCLUDE_HLSLI(TexturesHLSLI));
	}

	// Vertex shader.
	SS("void " + vertexShaderName + "(");
	SS("    in float4 iPosition : POSITION,");
	SS("    in float3 iNormal : NORMAL,");
	if (vertexUV) {
		SS("    in float2 iUV : TEXCOORD,");
	}
	for (int i = 0; i < cc.inputCount; i++) {
		const std::string floatNumber = cc.opt_alpha ? "4" : "3";
		SS("    in float" + floatNumber + " iInput" + std::to_string(i + 1) + " : COLOR" + std::to_string(i) + ",");
	}
	SS("    out float4 oPosition : SV_POSITION,");
	SS("    out float3 oNormal : NORMAL,");
	if (vertexUV) {
		SS("    out float2 oUV : TEXCOORD" + std::string((cc.inputCount > 0) ? "," : ""));
	}
	for (int i = 0; i < cc.inputCount; i++) {
		SS("    out float4 oInput" + std::to_string(i + 1) + " : COLOR" + std::to_string(i) + std::string(((i + 1) < cc.inputCount) ? "," : ""));
	}
	SS(") {");
	SS("    oPosition = iPosition;");
	SS("    oNormal = iNormal;");
	if (vertexUV) {
		SS("    oUV = iUV;");
	}
	for (int i = 0; i < cc.inputCount; i++) {
		SS("    oInput" + std::to_string(i + 1) + " = " + std::string(cc.opt_alpha ? "" : "float4(") + "iInput" + std::to_string(i + 1) + std::string(cc.opt_alpha ? "" : ", 1.0f)") + ";");
	}
	SS("}");

	// Pixel shader.
	SS("void " + pixelShaderName + "(");
	SS("    in float4 vertexPosition : SV_POSITION,");
	SS("    in float3 vertexNormal : NORMAL,");
	if (vertexUV) {
		SS("    in float2 vertexUV : TEXCOORD,");
	}
	for (int i = 0; i < cc.inputCount; i++) {
		SS("    in float4 input" + std::to_string(i + 1) + " : COLOR" + std::to_string(i) + ",");
	}
	SS("    out float4 resultColor : SV_TARGET");
	SS(") {");

	if (cc.useTextures[0]) {
		SS("    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;");
		SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleLevel(gTextureSampler, vertexUV, 0);");
	}

	if (cc.useTextures[1]) {
		// TODO
		SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
	}

	if (!cc.color_alpha_same && cc.opt_alpha) {
		SS("    resultColor = float4((" + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], false, true) + ").rgb, " + alphaFormula(cc.c, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true) + ");");
	}
	else {
		SS("    resultColor = " + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha) + ";");
	}
	SS("}");

	std::string shaderCode = ss.str();
	rasterGroup.pixelShaderName = win32::Utf8ToUtf16(pixelShaderName);
	rasterGroup.vertexShaderName = win32::Utf8ToUtf16(vertexShaderName);
	compileShaderCode(shaderCode, rasterGroup.pixelShaderName, L"ps_6_3", &rasterGroup.blobPS);
	compileShaderCode(shaderCode, rasterGroup.vertexShaderName, L"vs_6_3", &rasterGroup.blobVS);
	rasterGroup.rootSignature = generateRasterRootSignature(filter, hAddr, vAddr, samplerRegisterIndex);

	// Define the vertex layout.
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
	inputElementDescs.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)(vl.positionOffset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	inputElementDescs.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)(vl.normalOffset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	if (vertexUV) {
		inputElementDescs.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)(vl.uvOffset), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	}
	for (int i = 0; i < cc.inputCount; i++) {
		inputElementDescs.push_back({ "COLOR", (UINT)(i), cc.opt_alpha ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)(vl.inputOffset[i]), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	}

	// Blend state.
	const D3D12_RENDER_TARGET_BLEND_DESC alphaBlendDesc = {
		TRUE, FALSE,
		D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};

	D3D12_BLEND_DESC bd = {};
	bd.AlphaToCoverageEnable = FALSE;
	bd.IndependentBlendEnable = FALSE;
	for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
		bd.RenderTarget[i] = alphaBlendDesc;
	}

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = bd;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.InputLayout = { inputElementDescs.data(), (UINT)(inputElementDescs.size()) };
	psoDesc.pRootSignature = rasterGroup.rootSignature;
	psoDesc.VS.BytecodeLength = rasterGroup.blobVS->GetBufferSize();
	psoDesc.VS.pShaderBytecode = rasterGroup.blobVS->GetBufferPointer();
	psoDesc.PS.BytecodeLength = rasterGroup.blobPS->GetBufferSize();
	psoDesc.PS.pShaderBytecode = rasterGroup.blobPS->GetBufferPointer();
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	D3D12_CHECK(device->getD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&rasterGroup.pipelineState)));
}

void RT64::Shader::generateSurfaceHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, bool normalMapEnabled, bool specularMapEnabled, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName) {
	ColorCombinerParams cc(shaderId);

	std::stringstream ss;
	incMeshBuffers(ss);

	SS(INCLUDE_HLSLI(MaterialsHLSLI));
	SS(INCLUDE_HLSLI(InstancesHLSLI));
	SS(INCLUDE_HLSLI(GlobalHitBuffersHLSLI));
	SS(INCLUDE_HLSLI(RayHLSLI));
	SS(INCLUDE_HLSLI(RandomHLSLI));
	SS(INCLUDE_HLSLI(GlobalParamsHLSLI));

	unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
	if (cc.useTextures[0]) {
		SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
		SS(INCLUDE_HLSLI(TexturesHLSLI));
	}

	SS("[shader(\"anyhit\")]");
	SS("void " << anyHitName << "(inout HitInfo payload, Attributes attrib) {");
	SS("    uint instanceId = InstanceIndex();");
	SS("    uint triangleIndex = PrimitiveIndex();");
	SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");
	SS("    float4 diffuseColorMix = instanceMaterials[instanceId].diffuseColorMix;");

	bool vertexUV = cc.useTextures[0] || cc.useTextures[1];
	getVertexData(ss, true, true, vertexUV, cc.inputCount, cc.opt_alpha, vertexUV && normalMapEnabled);

	if (cc.useTextures[0]) {
		SS("    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;");
		SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleLevel(gTextureSampler, vertexUV, mipLevelBias);");
		SS("    texVal0.rgb = lerp(texVal0.rgb, diffuseColorMix.rgb, max(-diffuseColorMix.a, 0.0f));");
	}

	if (cc.useTextures[1]) {
		// TODO
		SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
	}

	if (!cc.color_alpha_same && cc.opt_alpha) {
		SS("    float4 resultColor = float4((" + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], false, true) + ").rgb, " + alphaFormula(cc.c, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true) + ");");
	}
	else {
		SS("    float4 resultColor = " + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha) + ";");
	}

	// Only mix the final diffuse color if the alpha is positive.
	SS("    resultColor.rgb = lerp(resultColor.rgb, diffuseColorMix.rgb, max(diffuseColorMix.a, 0.0f));");

	// Apply the solid alpha multiplier.
	SS("    resultColor.a = clamp(instanceMaterials[instanceId].solidAlphaMultiplier * resultColor.a, 0.0f, 1.0f);");

#ifdef TEXTURE_EDGE_ENABLED
	if (cc.opt_texture_edge) {
		SS("    if (resultColor.a > 0.3f) {");
		SS("      resultColor.a = 1.0f;");
		SS("    }");
		SS("    else {");
		SS("      IgnoreHit();");
		SS("    }");
	}
#endif

	if (cc.opt_noise) {
		SS("    uint seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frameCount, 16);");
		SS("    resultColor.a *= round(nextRand(seed));");
	}

	if (vertexUV && normalMapEnabled) {
		SS("    int normalTexIndex = instanceMaterials[instanceId].normalTexIndex;");
		SS("    if (normalTexIndex >= 0) {");
		SS("        float uvDetailScale = instanceMaterials[instanceId].uvDetailScale;");
		SS("        float3 normalColor = gTextures[NonUniformResourceIndex(normalTexIndex)].SampleLevel(gTextureSampler, vertexUV * uvDetailScale, mipLevelBias).xyz;");
		SS("        normalColor = (normalColor * 2.0f) - 1.0f;");
		SS("        float3 newNormal = normalize(vertexNormal * normalColor.z + vertexTangent * normalColor.x + vertexBinormal * normalColor.y);");
		SS("        vertexNormal = newNormal;");
		SS("    }");
	}

	SS("	float3 prevWorldPos = mul(instanceTransforms[instanceId].objectToWorldPrevious, float4(vertexPosition, 1.0f));");
	SS("	float3 curWorldPos = mul(instanceTransforms[instanceId].objectToWorld, float4(vertexPosition, 1.0f));");
	SS("	float3 vertexFlow = curWorldPos - prevWorldPos;");

	transformVertexNormal(ss);

	SS("    float3 vertexSpecular = float3(1.0f, 1.0f, 1.0f);");
	if (vertexUV && specularMapEnabled) {
		SS("    int specularTexIndex = instanceMaterials[instanceId].specularTexIndex;");
		SS("    if (specularTexIndex >= 0) {");
		SS("        float uvDetailScale = instanceMaterials[instanceId].uvDetailScale;");
		SS("        vertexSpecular = gTextures[NonUniformResourceIndex(specularTexIndex)].SampleLevel(gTextureSampler, vertexUV * uvDetailScale, mipLevelBias).rgb;");
		SS("    }");
	}

	SS("    uint2 pixelIdx = DispatchRaysIndex().xy;");
	SS("    uint2 pixelDims = DispatchRaysDimensions().xy;");
	SS("    uint hitStride = pixelDims.x * pixelDims.y;");

	// HACK: Add some bias for the comparison based on the instance ID so coplanar surfaces are friendlier with each other.
	// This can likely be implemented as an instance property at some point to control depth sorting.
	SS("    float tval = WithDistanceBias(RayTCurrent(), instanceId);");
	SS("    uint hi = getHitBufferIndex(min(payload.nhits, MAX_HIT_QUERIES), pixelIdx, pixelDims);");
	SS("    uint minHi = getHitBufferIndex(payload.ohits, pixelIdx, pixelDims);");
	SS("    uint lo = hi - hitStride;");
	SS("    while ((hi > minHi) && (tval < gHitDistAndFlow[lo].x)) {");
	SS("        gHitDistAndFlow[hi] = gHitDistAndFlow[lo];");
	SS("        gHitColor[hi] = gHitColor[lo];");
	SS("        gHitNormal[hi] = gHitNormal[lo];");
	SS("        gHitSpecular[hi] = gHitSpecular[lo];");
	SS("        gHitInstanceId[hi] = gHitInstanceId[lo];");
	SS("        hi -= hitStride;");
	SS("        lo -= hitStride;");
	SS("    }");
	SS("    uint hitPos = hi / hitStride;");
	SS("    if (hitPos < MAX_HIT_QUERIES) {");
	SS("        gHitDistAndFlow[hi] = float4(tval, vertexFlow);");
	SS("        gHitColor[hi] = resultColor;");
	SS("        gHitNormal[hi] = float4(vertexNormal, 1.0f);");
	SS("        gHitSpecular[hi] = float4(vertexSpecular, 1.0f);");
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
	compileShaderCode(shaderCode, L"", L"lib_6_3", &surfaceHitGroup.blob);
	surfaceHitGroup.rootSignature = generateHitRootSignature(filter, hAddr, vAddr, samplerRegisterIndex, true);
	surfaceHitGroup.hitGroupName = win32::Utf8ToUtf16(hitGroupName);
	surfaceHitGroup.closestHitName = win32::Utf8ToUtf16(closestHitName);
	surfaceHitGroup.anyHitName = win32::Utf8ToUtf16(anyHitName);
}

void RT64::Shader::generateShadowHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName) {
	ColorCombinerParams cc(shaderId);
	std::stringstream ss;
	incMeshBuffers(ss);
	
	SS(INCLUDE_HLSLI(MaterialsHLSLI));
	SS(INCLUDE_HLSLI(InstancesHLSLI));
	SS(INCLUDE_HLSLI(RayHLSLI));
	SS(INCLUDE_HLSLI(RandomHLSLI));
	SS(INCLUDE_HLSLI(GlobalParamsHLSLI));

	unsigned int samplerRegisterIndex = uniqueSamplerRegisterIndex(filter, hAddr, vAddr);
	if (cc.useTextures[0]) {
		SS("SamplerState gTextureSampler : register(s" + std::to_string(samplerRegisterIndex) + ");");
		SS(INCLUDE_HLSLI(TexturesHLSLI));
	}
	
	SS("[shader(\"anyhit\")]");
	SS("void " << anyHitName << "(inout ShadowHitInfo payload, Attributes attrib) {");
	if (cc.opt_alpha) {
		SS("    uint instanceId = InstanceIndex();");
		SS("    uint triangleIndex = PrimitiveIndex();");
		SS("    float3 barycentrics = float3((1.0f - attrib.bary.x - attrib.bary.y), attrib.bary.x, attrib.bary.y);");

		getVertexData(ss, true, true, cc.useTextures[0] || cc.useTextures[1], cc.inputCount, cc.opt_alpha, false);

		if (cc.useTextures[0]) {
			SS("    int diffuseTexIndex = instanceMaterials[instanceId].diffuseTexIndex;");
			SS("    float4 texVal0 = gTextures[NonUniformResourceIndex(diffuseTexIndex)].SampleLevel(gTextureSampler, vertexUV, 0);");
		}

		if (cc.useTextures[1]) {
			// TODO
			SS("    float4 texVal1 = float4(1.0f, 0.0f, 1.0f, 1.0f);");
		}

		if (!cc.color_alpha_same && cc.opt_alpha) {
			SS("    float resultAlpha = " + alphaFormula(cc.c, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true) + ";");
		}
		else {
			SS("    float resultAlpha = (" + colorFormula(cc.c, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha) + ").a;");
		}

		SS("    resultAlpha = clamp(resultAlpha * instanceMaterials[instanceId].shadowAlphaMultiplier, 0.0f, 1.0f);");

#ifdef TEXTURE_EDGE_ENABLED
		if (cc.opt_texture_edge) {
			SS("    if (resultAlpha > 0.3f) {");
			SS("      resultAlpha = 1.0f;");
			SS("    }");
			SS("    else {");
			SS("      IgnoreHit();");
			SS("    }");
		}
#endif

		if (cc.opt_noise) {
			SS("    uint seed = initRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, frameCount, 16);");
			SS("    resultAlpha *= round(nextRand(seed));");
		}

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
	compileShaderCode(shaderCode, L"", L"lib_6_3", &shadowHitGroup.blob);
	shadowHitGroup.rootSignature = generateHitRootSignature(filter, hAddr, vAddr, samplerRegisterIndex, false);
	shadowHitGroup.hitGroupName = win32::Utf8ToUtf16(hitGroupName);
	shadowHitGroup.closestHitName = win32::Utf8ToUtf16(closestHitName);
	shadowHitGroup.anyHitName = win32::Utf8ToUtf16(anyHitName);
}

void RT64::Shader::fillSamplerDesc(D3D12_STATIC_SAMPLER_DESC &desc, Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex) {
	desc.Filter = toD3DTexFilter(filter);
	desc.AddressU = toD3DTexAddr(hAddr);
	desc.AddressV = toD3DTexAddr(vAddr);
	desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	desc.MinLOD = 0;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	desc.MipLODBias = 0.0f;
	desc.MaxAnisotropy = 1;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	desc.ShaderRegister = samplerRegisterIndex;
	desc.RegisterSpace = 0;
}

ID3D12RootSignature *RT64::Shader::generateRasterRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex) {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	nv_helpers_dx12::RootSignatureGenerator::HeapRanges heapRanges;
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 0);
	heapRanges.push_back({ SRV_INDEX(instanceTransforms), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceTransforms) });
	heapRanges.push_back({ SRV_INDEX(instanceMaterials), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceMaterials) });
	heapRanges.push_back({ SRV_INDEX(gTextures), 512, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(gTextures) });
	rsc.AddHeapRangesParameter(heapRanges);

	D3D12_STATIC_SAMPLER_DESC samplerDesc;
	fillSamplerDesc(samplerDesc, filter, hAddr, vAddr, samplerRegisterIndex);
	return rsc.Generate(device->getD3D12Device(), false, true, &samplerDesc, 1);
}

ID3D12RootSignature *RT64::Shader::generateHitRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex, bool hitBuffers) {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	nv_helpers_dx12::RootSignatureGenerator::HeapRanges heapRanges;
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, SRV_INDEX(vertexBuffer));
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, SRV_INDEX(indexBuffer));

	if (hitBuffers) {
		heapRanges.push_back({ UAV_INDEX(gHitDistAndFlow), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitDistAndFlow) });
		heapRanges.push_back({ UAV_INDEX(gHitColor), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitColor) });
		heapRanges.push_back({ UAV_INDEX(gHitNormal), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitNormal) });
		heapRanges.push_back({ UAV_INDEX(gHitSpecular), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitSpecular) });
		heapRanges.push_back({ UAV_INDEX(gHitInstanceId), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitInstanceId) });
	}

	heapRanges.push_back({ SRV_INDEX(instanceTransforms), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceTransforms) });
	heapRanges.push_back({ SRV_INDEX(instanceMaterials), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceMaterials) });
	heapRanges.push_back({ SRV_INDEX(gTextures), 512, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(gTextures) });
	heapRanges.push_back({ CBV_INDEX(gParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, HEAP_INDEX(gParams) });
	rsc.AddHeapRangesParameter(heapRanges);

	D3D12_STATIC_SAMPLER_DESC samplerDesc;
	fillSamplerDesc(samplerDesc, filter, hAddr, vAddr, samplerRegisterIndex);
	return rsc.Generate(device->getD3D12Device(), true, false, &samplerDesc, 1);
}

void RT64::Shader::compileShaderCode(const std::string &shaderCode, const std::wstring &entryName, const std::wstring &profile, IDxcBlob **shaderBlob) {
#ifndef NDEBUG
	fprintf(stdout, "Compiling...\n\n%s\n", shaderCode.c_str());
#endif

    IDxcBlobEncoding *textBlob = nullptr;
    D3D12_CHECK(device->getDxcLibrary()->CreateBlobWithEncodingFromPinned((LPBYTE)shaderCode.c_str(), (uint32_t)shaderCode.size(), 0, &textBlob));

	std::vector<LPCWSTR> arguments;
	arguments.push_back(L"-Qstrip_debug");
	arguments.push_back(L"-Qstrip_reflect");

    IDxcOperationResult *result = nullptr;
    D3D12_CHECK(device->getDxcCompiler()->Compile(textBlob, L"", entryName.c_str(), profile.c_str(), arguments.data(), (UINT32)(arguments.size()), nullptr, 0, nullptr, &result));

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

const RT64::Shader::RasterGroup &RT64::Shader::getRasterGroup() const {
	return rasterGroup;
}

RT64::Shader::HitGroup &RT64::Shader::getSurfaceHitGroup() {
	return surfaceHitGroup;
}

RT64::Shader::HitGroup &RT64::Shader::getShadowHitGroup() {
	return shadowHitGroup;
}

bool RT64::Shader::hasRasterGroup() const {
	return (rasterGroup.blobPS != nullptr) || (rasterGroup.blobVS != nullptr);
}

bool RT64::Shader::hasHitGroups() const {
	return (surfaceHitGroup.blob != nullptr) || (shadowHitGroup.blob != nullptr);
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

DLLEXPORT RT64_SHADER *RT64_CreateShader(RT64_DEVICE *devicePtr, unsigned int shaderId, unsigned int filter, unsigned int hAddr, unsigned int vAddr, int flags) {
    try {
        RT64::Device *device = (RT64::Device *)(devicePtr);
		RT64::Shader::Filter sFilter = convertFilter(filter);
		RT64::Shader::AddressingMode sHAddr = convertAddressingMode(hAddr);
		RT64::Shader::AddressingMode sVAddr = convertAddressingMode(vAddr);
        return (RT64_SHADER *)(new RT64::Shader(device, shaderId, sFilter, sHAddr, sVAddr, flags));
    }
    RT64_CATCH_EXCEPTION();
    return nullptr;
}

DLLEXPORT void RT64_DestroyShader(RT64_SHADER *shaderPtr) {
	delete (RT64::Shader *)(shaderPtr);
}

#endif