//
// RT64
//

#include <cassert>

#include <dwmapi.h>

#include "utf8conv/utf8conv.h"

#include "rt64_device.h"

#ifndef RT64_MINIMAL
#include "rt64_inspector.h"
#include "rt64_scene.h"
#include "rt64_shader.h"
#include "rt64_texture.h"

#include "shaders/DirectRayGen.hlsl.h"
#include "shaders/IndirectRayGen.hlsl.h"
#include "shaders/ReflectionRayGen.hlsl.h"
#include "shaders/RefractionRayGen.hlsl.h"
#include "shaders/PrimaryRayGen.hlsl.h"

#include "shaders/FsrEasuPassCS.hlsl.h"
#include "shaders/FsrRcasPassCS.hlsl.h"

#include "shaders/FullScreenVS.hlsl.h"
#include "shaders/Im3DVS.hlsl.h"

#include "shaders/Im3DGSPoints.hlsl.h"
#include "shaders/Im3DGSLines.hlsl.h"

#include "shaders/ComposePS.hlsl.h"
#include "shaders/DebugPS.hlsl.h"
#include "shaders/Im3DPS.hlsl.h"
#include "shaders/PostProcessPS.hlsl.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#endif

// Private

RT64::Device::Device(HWND hwnd) {
	RT64_LOG_OPEN("rt64.log");

	createDXGIFactory();
	createRaytracingDevice();

#ifndef RT64_MINIMAL
	assert(hwnd != 0);
	this->hwnd = hwnd;
	d3dAllocator = nullptr;
	d3dCommandListOpen = true;
	d3dRtStateObject = nullptr;
	lastCommandQueueBarrierActive = false;
	lastCopyQueueBarrierActive = false;
	d3dRenderTargets[0] = nullptr;
	d3dRenderTargets[1] = nullptr;
	d3dRenderTargetReadbackRowWidth = 0;
	d3dRtStateObjectDirty = false;
	d3dPrimaryRayGenLibrary = nullptr;
	d3dDirectRayGenLibrary = nullptr;
	d3dIndirectRayGenLibrary = nullptr;
	d3dReflectionRayGenLibrary = nullptr;
	d3dRefractionRayGenLibrary = nullptr;
	primaryRayGenID = nullptr;
	directRayGenID = nullptr;
	indirectRayGenID = nullptr;
	reflectionRayGenID = nullptr;
	refractionRayGenID = nullptr;
	surfaceMissID = nullptr;
	shadowMissID = nullptr;
	width = 0;
	height = 0;

	updateSize();
	loadPipeline();
	loadAssets();
	createDxcCompiler();
	createRaytracingPipeline();
#endif

	RT64_LOG_PRINTF("Created device");
}

RT64::Device::~Device() {
	RT64_LOG_CLOSE();

	/* TODO: Re-enable once resources are properly released.
	if (d3dAllocator != nullptr) {
		d3dAllocator->Release();
	}
	*/
}

void RT64::Device::createDXGIFactory() {
	UINT dxgiFactoryFlags = 0;
	dxgiFactory = nullptr;

#ifndef NDEBUG
	ID3D12Debug *debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();

		// Enable additional debug layers.
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif

	D3D12_CHECK(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));
}

void RT64::Device::createRaytracingDevice() {
	d3dAdapter = nullptr;
	d3dDevice = nullptr;

	std::stringstream ss;
	{
		// Attempt to create D3D12 devices and pick the first one that actually supports raytracing.
		// This implementation should detect more accurately cases where multiple D3D12 adapters are available
		// but they're not raytracing capable, yet there's more devices on the system that fit the criteria.
		DXGI_ADAPTER_DESC1 desc;
		for (UINT adapterIndex = 0; dxgiFactory->EnumAdapters1(adapterIndex, &d3dAdapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
			d3dAdapter->GetDesc1(&desc);

			// Ignore software adapters.
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				d3dAdapter->Release();
				d3dAdapter = nullptr;
				continue;
			}

			auto handleAdapterError = [this, &ss, &desc, &adapterIndex](const std::string &errorSuffix) {
				ss << "Adapter " << win32::Utf16ToUtf8(desc.Description) << " (#" << adapterIndex << "): " << errorSuffix << std::endl;
				if (d3dDevice != nullptr) {
					d3dDevice->Release();
					d3dDevice = nullptr;
				}

				if (d3dAdapter != nullptr) {
					d3dAdapter->Release();
					d3dAdapter = nullptr;
				}
			};

			// Try creating the device for this adapter.
			HRESULT deviceResult = D3D12CreateDevice(d3dAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&d3dDevice));
			if (SUCCEEDED(deviceResult)) {
				D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
				HRESULT checkResult = d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
				if (SUCCEEDED(checkResult)) {
					if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
						handleAdapterError("No raytracing support.");
					}
					else {
						break;
					}
				}
				else {
					handleAdapterError("No feature checking at the required level.");
					ss << "D3D12Device->CheckFeatureSupport error code: " << std::hex << checkResult << std::endl;
				}
			}
			else {
				handleAdapterError("No D3D12.1 feature level support.");
				ss << "D3D12CreateDevice error code: " << std::hex << deviceResult << std::endl;
			}
		}
	}

	// Only throw an exception if no device was detected.
	if (d3dDevice == nullptr) {
		throw std::runtime_error("Unable to detect a device capable of raytracing.\n" + ss.str());
	}
}

#ifndef RT64_MINIMAL

void RT64::Device::updateSize() {
	RT64_LOG_PRINTF("Starting device size update");

	RECT rect;
	GetClientRect(hwnd, &rect);
	int newWidth = rect.right - rect.left;
	int newHeight = rect.bottom - rect.top;

	// Recrease the swap chain if the sizes have changed.
	if (((newWidth != width) || (newHeight != height)) && (newWidth > 0) && (newHeight > 0)) {
		width = newWidth;
		height = newHeight;
		aspectRatio = static_cast<float>(width) / static_cast<float>(height);
		d3dViewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
		d3dScissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));

		if (d3dSwapChain != nullptr) {
			releaseRTVs();
			D3D12_CHECK(d3dSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
			createRTVs();
			d3dFrameIndex = d3dSwapChain->GetCurrentBackBufferIndex();
		}

		for (Scene *scene : scenes) {
			scene->resize();
		}

		for (Inspector *inspector : inspectors) {
			inspector->resize();
		}
	}

	RT64_LOG_PRINTF("Finished device size update");
}

void RT64::Device::releaseRTVs() {
	if (d3dRtvHeap != nullptr) {
		d3dRtvHeap->Release();
		d3dRtvHeap = nullptr;
	}

	for (UINT n = 0; n < FrameCount; n++) {
		d3dRenderTargets[n]->Release();
		d3dRenderTargets[n] = nullptr;
	}

	d3dRenderTargetReadback.Release();
}

void RT64::Device::createRTVs() {
	// Describe and create a render target view (RTV) descriptor heap.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	D3D12_CHECK(d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&d3dRtvHeap)));
	
	d3dRtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(d3dRtvHeap->GetCPUDescriptorHandleForHeapStart());

	// Create a RTV for each frame.
	for (UINT n = 0; n < FrameCount; n++) {
		D3D12_CHECK(d3dSwapChain->GetBuffer(n, IID_PPV_ARGS(&d3dRenderTargets[n])));
		d3dDevice->CreateRenderTargetView(d3dRenderTargets[n], nullptr, rtvHandle);
		rtvHandle.Offset(1, d3dRtvDescriptorSize);
	}

	// Create the resource for render target readback.
	UINT rowPadding;
	CalculateTextureRowWidthPadding(width, 4, d3dRenderTargetReadbackRowWidth, rowPadding);

	D3D12_RESOURCE_DESC resDesc = { };
	resDesc.Format = DXGI_FORMAT_UNKNOWN;
	resDesc.Width = (d3dRenderTargetReadbackRowWidth * height);
	resDesc.Height = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	d3dRenderTargetReadback = allocateResource(D3D12_HEAP_TYPE_READBACK, &resDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
}

HWND RT64::Device::getHwnd() const {
	return hwnd;
}

ID3D12Device8 *RT64::Device::getD3D12Device() const {
	return d3dDevice;
}

ID3D12GraphicsCommandList4 *RT64::Device::getD3D12CommandList() const {
	return d3dCommandList;
}

ID3D12StateObject *RT64::Device::getD3D12RtStateObject() const {
	return d3dRtStateObject;
}

ID3D12StateObjectProperties *RT64::Device::getD3D12RtStateObjectProperties() const {
	return d3dRtStateObjectProps;
}

ID3D12Resource *RT64::Device::getD3D12RenderTarget() const {
	return d3dRenderTargets[d3dFrameIndex];
}

CD3DX12_CPU_DESCRIPTOR_HANDLE RT64::Device::getD3D12RTV() const {
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(d3dRtvHeap->GetCPUDescriptorHandleForHeapStart(), d3dFrameIndex, d3dRtvDescriptorSize);
}

ID3D12RootSignature *RT64::Device::getComposeRootSignature() const {
	return d3dComposeRootSignature;
}

ID3D12PipelineState *RT64::Device::getComposePipelineState() const {
	return d3dComposePipelineState;
}

ID3D12RootSignature *RT64::Device::getPostProcessRootSignature() const {
	return d3dPostProcessRootSignature;
}

ID3D12PipelineState *RT64::Device::getPostProcessPipelineState() const {
	return d3dPostProcessPipelineState;
}

ID3D12RootSignature *RT64::Device::getFsrEasuRootSignature() const {
	return d3dFsrEasuRootSignature;
}

ID3D12PipelineState *RT64::Device::getFsrEasuPipelineState() const {
	return d3dFsrEasuPipelineState;
}

ID3D12RootSignature *RT64::Device::getFsrRcasRootSignature() const {
	return d3dFsrRcasRootSignature;
}

ID3D12PipelineState *RT64::Device::getFsrRcasPipelineState() const {
	return d3dFsrRcasPipelineState;
}

ID3D12RootSignature *RT64::Device::getDebugRootSignature() const {
	return d3dDebugRootSignature;
}

ID3D12PipelineState *RT64::Device::getDebugPipelineState() const {
	return d3dDebugPipelineState;
}

ID3D12RootSignature *RT64::Device::getIm3dRootSignature() const {
	return im3dRootSignature;
}

ID3D12PipelineState *RT64::Device::getIm3dPipelineStatePoint() const {
	return im3dPipelineStatePoint;
}

ID3D12PipelineState *RT64::Device::getIm3dPipelineStateLine() const {
	return im3dPipelineStateLine;
}

ID3D12PipelineState *RT64::Device::getIm3dPipelineStateTriangle() const {
	return im3dPipelineStateTriangle;
}

void *RT64::Device::getPrimaryRayGenID() const {
	return primaryRayGenID;
}

void *RT64::Device::getDirectRayGenID() const {
	return directRayGenID;
}

void *RT64::Device::getIndirectRayGenID() const {
	return indirectRayGenID;
}

void *RT64::Device::getReflectionRayGenID() const {
	return reflectionRayGenID;
}

void *RT64::Device::getRefractionRayGenID() const {
	return refractionRayGenID;
}

void *RT64::Device::getSurfaceMissID() const {
	return surfaceMissID;
}

void *RT64::Device::getShadowMissID() const {
	return shadowMissID;
}

IDxcCompiler *RT64::Device::getDxcCompiler() const {
	return d3dDxcCompiler;
}

IDxcLibrary *RT64::Device::getDxcLibrary() const {
	return d3dDxcLibrary;
}

CD3DX12_VIEWPORT RT64::Device::getD3D12Viewport() const {
	return d3dViewport;
}

CD3DX12_RECT RT64::Device::getD3D12ScissorRect() const {
	return d3dScissorRect;
}

RT64::AllocatedResource RT64::Device::allocateResource(D3D12_HEAP_TYPE HeapType, _In_  const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, _In_opt_  const D3D12_CLEAR_VALUE *pOptimizedClearValue, bool committed, bool shared) {
	D3D12MA::ALLOCATION_DESC allocationDesc = {};
	allocationDesc.HeapType = HeapType;
	allocationDesc.ExtraHeapFlags = shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
	allocationDesc.Flags = committed ? D3D12MA::ALLOCATION_FLAG_COMMITTED : D3D12MA::ALLOCATION_FLAG_NONE;

	D3D12MA::Allocation *allocation = nullptr;
	ID3D12Resource *resource = nullptr;
	d3dAllocator->CreateResource(&allocationDesc, pDesc, InitialResourceState, pOptimizedClearValue, &allocation, IID_PPV_ARGS(&resource));
	return AllocatedResource(allocation);
}

RT64::AllocatedResource RT64::Device::allocateBuffer(D3D12_HEAP_TYPE HeapType, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES InitialResourceState, bool committed, bool shared) {
	D3D12MA::ALLOCATION_DESC allocationDesc = {};
	allocationDesc.HeapType = HeapType;
	allocationDesc.ExtraHeapFlags = shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
	allocationDesc.Flags = committed ? D3D12MA::ALLOCATION_FLAG_COMMITTED : D3D12MA::ALLOCATION_FLAG_NONE;

	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = flags;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = size;

	D3D12MA::Allocation *allocation = nullptr;
	ID3D12Resource *resource = nullptr;
	d3dAllocator->CreateResource(&allocationDesc, &bufDesc, InitialResourceState, nullptr, &allocation, IID_PPV_ARGS(&resource));
	return AllocatedResource(allocation);
}

void RT64::Device::setLastCommandQueueBarrier(const D3D12_RESOURCE_BARRIER &barrier) {
	lastCommandQueueBarrier = barrier;
	lastCommandQueueBarrierActive = true;
}

void RT64::Device::submitCommandQueueBarrier() {
	if (lastCommandQueueBarrierActive) {
		d3dCommandList->ResourceBarrier(1, &lastCommandQueueBarrier);
		lastCommandQueueBarrierActive = false;
	}
}

void RT64::Device::setLastCopyQueueBarrier(const D3D12_RESOURCE_BARRIER &barrier) {
	lastCopyQueueBarrier = barrier;
	lastCopyQueueBarrierActive = true;
}

void RT64::Device::submitCopyQueueBarrier() {
	if (lastCopyQueueBarrierActive) {
		d3dCommandList->ResourceBarrier(1, &lastCopyQueueBarrier);
		lastCopyQueueBarrierActive = false;
	}
}

int RT64::Device::getWidth() const {
	return width;
}

int RT64::Device::getHeight() const {
	return height;
}

float RT64::Device::getAspectRatio() const {
	return aspectRatio;
}

void RT64::Device::loadPipeline() {
	RT64_LOG_PRINTF("Pipeline load started");

	// Create memory allocator.
	D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
	allocatorDesc.pDevice = d3dDevice;
	allocatorDesc.pAdapter = d3dAdapter;

	D3D12_CHECK(D3D12MA::CreateAllocator(&allocatorDesc, &d3dAllocator));

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	D3D12_CHECK(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d3dCommandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	IDXGISwapChain1 *swapChain;
	D3D12_CHECK(dxgiFactory->CreateSwapChainForHwnd(d3dCommandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &swapChain));
	D3D12_CHECK(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

	d3dSwapChain = static_cast<IDXGISwapChain3 *>(swapChain);
	d3dFrameIndex = d3dSwapChain->GetCurrentBackBufferIndex();

	createRTVs();

	D3D12_CHECK(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&d3dCommandAllocator)));

	RT64_LOG_PRINTF("Pipeline load finished");
}

void RT64::Device::loadAssets() {
	RT64_LOG_PRINTF("Asset load started");

	const D3D12_RENDER_TARGET_BLEND_DESC alphaBlendDesc = {
		TRUE, FALSE,
		D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};

	auto setPsoDefaults = [](D3D12_GRAPHICS_PIPELINE_STATE_DESC &psoDesc, const D3D12_RENDER_TARGET_BLEND_DESC &blendDesc, DXGI_FORMAT rtvFormat) {
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		D3D12_BLEND_DESC bd = {};
		bd.AlphaToCoverageEnable = FALSE;
		bd.IndependentBlendEnable = FALSE;

		for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
			bd.RenderTarget[i] = blendDesc;
		}

		psoDesc.BlendState = bd;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = rtvFormat;
		psoDesc.SampleDesc.Count = 1;
	};

	RT64_LOG_PRINTF("Creating the Im3d root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter({
			{ UAV_INDEX(gHitDistAndFlow), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitDistAndFlow) },
			{ UAV_INDEX(gHitColor), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitColor) },
			{ UAV_INDEX(gHitNormal), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitNormal) },
			{ UAV_INDEX(gHitSpecular), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitSpecular) },
			{ UAV_INDEX(gHitInstanceId), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitInstanceId) },
			{ CBV_INDEX(gParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, HEAP_INDEX(gParams) }
		});

		im3dRootSignature = rsc.Generate(d3dDevice, false, true, nullptr, 0);
	}

	RT64_LOG_PRINTF("Creating the Im3d pipeline state");
	{
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION_SIZE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0  }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		setPsoDefaults(psoDesc, alphaBlendDesc, DXGI_FORMAT_R8G8B8A8_UNORM);

		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = im3dRootSignature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(Im3DVSBlob, sizeof(Im3DVSBlob));
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(Im3DPSBlob, sizeof(Im3DPSBlob));

		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		D3D12_CHECK(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&im3dPipelineStateTriangle)));

		psoDesc.GS = CD3DX12_SHADER_BYTECODE(Im3DGSPointsBlob, sizeof(Im3DGSPointsBlob));
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		D3D12_CHECK(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&im3dPipelineStatePoint)));

		psoDesc.GS = CD3DX12_SHADER_BYTECODE(Im3DGSLinesBlob, sizeof(Im3DGSLinesBlob));
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		D3D12_CHECK(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&im3dPipelineStateLine)));
	}

	RT64_LOG_PRINTF("Creating the compose root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter({
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 },
			{ 1, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1 },
			{ 2, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2 },
			{ 3, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3 },
			{ 4, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4 },
			{ 5, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5 },
			{ 6, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6 },
			{ CBV_INDEX(gParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 7 }
		});

		// Fill out the sampler.
		D3D12_STATIC_SAMPLER_DESC desc;
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D12_FLOAT32_MAX;
		desc.MipLODBias = 0.0f;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		desc.ShaderRegister = 0;
		desc.RegisterSpace = 0;
		d3dComposeRootSignature = rsc.Generate(d3dDevice, false, true, &desc, 1);
	}

	RT64_LOG_PRINTF("Creating the compose pipeline state");
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		setPsoDefaults(psoDesc, alphaBlendDesc, DXGI_FORMAT_R32G32B32A32_FLOAT);
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = d3dComposeRootSignature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(FullScreenVSBlob, sizeof(FullScreenVSBlob));
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(ComposePSBlob, sizeof(ComposePSBlob));
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		D3D12_CHECK(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&d3dComposePipelineState)));
	}

	RT64_LOG_PRINTF("Creating the post process root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter({
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 },
			{ 1, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1 },
			{ CBV_INDEX(gParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2 }
		});

		// Fill out the sampler.
		D3D12_STATIC_SAMPLER_DESC desc;
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D12_FLOAT32_MAX;
		desc.MipLODBias = 0.0f;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		desc.ShaderRegister = 0;
		desc.RegisterSpace = 0;
		d3dPostProcessRootSignature = rsc.Generate(d3dDevice, false, true, &desc, 1);
	}

	RT64_LOG_PRINTF("Creating the post process pipeline state");
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		setPsoDefaults(psoDesc, alphaBlendDesc, DXGI_FORMAT_R8G8B8A8_UNORM);
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = d3dPostProcessRootSignature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(FullScreenVSBlob, sizeof(FullScreenVSBlob));
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(PostProcessPSBlob, sizeof(PostProcessPSBlob));
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		D3D12_CHECK(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&d3dPostProcessPipelineState)));
	}

	RT64_LOG_PRINTF("Creating the debug root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter({
			{ UAV_INDEX(gShadingPosition), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gShadingPosition) },
			{ UAV_INDEX(gShadingNormal), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gShadingNormal) },
			{ UAV_INDEX(gShadingSpecular), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gShadingSpecular) },
			{ UAV_INDEX(gDiffuse), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gDiffuse) },
			{ UAV_INDEX(gInstanceId), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gInstanceId) },
			{ UAV_INDEX(gDirectLight), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gDirectLight) },
			{ UAV_INDEX(gIndirectLight), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gIndirectLight) },
			{ UAV_INDEX(gReflection), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gReflection) },
			{ UAV_INDEX(gRefraction), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gRefraction) },
			{ UAV_INDEX(gTransparent), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gTransparent) },
			{ UAV_INDEX(gFlow), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gFlow) },
			{ UAV_INDEX(gDepth), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gDepth) },
			{ CBV_INDEX(gParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, HEAP_INDEX(gParams) }
		});

		d3dDebugRootSignature = rsc.Generate(d3dDevice, false, true, nullptr, 0);
	}

	RT64_LOG_PRINTF("Creating the debug pipeline state");
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		setPsoDefaults(psoDesc, alphaBlendDesc, DXGI_FORMAT_R8G8B8A8_UNORM);
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = d3dDebugRootSignature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(FullScreenVSBlob, sizeof(FullScreenVSBlob));
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(DebugPSBlob, sizeof(DebugPSBlob));
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		D3D12_CHECK(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&d3dDebugPipelineState)));
	}

	RT64_LOG_PRINTF("Creating the FSR EASU root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter({
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 },
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1 },
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2 }
		});

		// Fill out the sampler.
		D3D12_STATIC_SAMPLER_DESC desc = { };
		desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.MaxAnisotropy = 1;
		desc.MaxLOD = D3D12_FLOAT32_MAX;

		d3dFsrEasuRootSignature = rsc.Generate(d3dDevice, false, true, &desc, 1);
	}

	RT64_LOG_PRINTF("Creating the FSR EASU pipeline state");
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(FsrEasuPassCSBlob, sizeof(FsrEasuPassCSBlob));
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = d3dFsrEasuRootSignature;
		psoDesc.NodeMask = 0;

		D3D12_CHECK(d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&d3dFsrEasuPipelineState)));
	}

	RT64_LOG_PRINTF("Creating the FSR RCAS root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter({
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 },
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1 },
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2 }
		});

		// Fill out the sampler.
		D3D12_STATIC_SAMPLER_DESC desc = { };
		desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.MaxAnisotropy = 1;
		desc.MaxLOD = D3D12_FLOAT32_MAX;

		d3dFsrRcasRootSignature = rsc.Generate(d3dDevice, false, true, &desc, 1);
	}

	RT64_LOG_PRINTF("Creating the FSR RCAS pipeline state");
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(FsrRcasPassCSBlob, sizeof(FsrRcasPassCSBlob));
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = d3dFsrRcasRootSignature;
		psoDesc.NodeMask = 0;

		D3D12_CHECK(d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&d3dFsrRcasPipelineState)));
	}

	RT64_LOG_PRINTF("Creating the command list");

	// Create the command list.
	D3D12_CHECK(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3dCommandAllocator, nullptr, IID_PPV_ARGS(&d3dCommandList)));


	RT64_LOG_PRINTF("Creating the fence");

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	D3D12_CHECK(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d3dFence)));
	d3dFenceValue = 1;

	// Create an event handle to use for frame synchronization.
	d3dFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (d3dFenceEvent == nullptr) {
		D3D12_CHECK(HRESULT_FROM_WIN32(GetLastError()));
	}

	RT64_LOG_PRINTF("Waiting for asset load to finish");

	// Close command list and wait for it to finish.
	waitForGPU();

	RT64_LOG_PRINTF("Asset load finished");
}

void RT64::Device::createRaytracingPipeline() {
	RT64_LOG_PRINTF("Raytracing pipeline creation started");
	if (d3dRtStateObject != nullptr) {
		d3dRtStateObject->Release();
		d3dRtStateObject = nullptr;
	}

	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(d3dDevice);

	RT64_LOG_PRINTF("Loading shader libraries");

	// Shader libraries.
	if (d3dPrimaryRayGenLibrary == nullptr) {
		d3dPrimaryRayGenLibrary = new StaticBlob(PrimaryRayGenBlob, sizeof(PrimaryRayGenBlob));
	}

	if (d3dDirectRayGenLibrary == nullptr) {
		d3dDirectRayGenLibrary = new StaticBlob(DirectRayGenBlob, sizeof(DirectRayGenBlob));
	}

	if (d3dIndirectRayGenLibrary == nullptr) {
		d3dIndirectRayGenLibrary = new StaticBlob(IndirectRayGenBlob, sizeof(IndirectRayGenBlob));
	}

	if (d3dReflectionRayGenLibrary == nullptr) {
		d3dReflectionRayGenLibrary = new StaticBlob(ReflectionRayGenBlob, sizeof(ReflectionRayGenBlob));
	}

	if (d3dRefractionRayGenLibrary == nullptr) {
		d3dRefractionRayGenLibrary = new StaticBlob(RefractionRayGenBlob, sizeof(RefractionRayGenBlob));
	}

	RT64_LOG_PRINTF("Adding libraries");

	// Add shaders from libraries to the pipeline.
	pipeline.AddLibrary(d3dPrimaryRayGenLibrary, { L"PrimaryRayGen", L"SurfaceMiss", L"ShadowMiss" });
	pipeline.AddLibrary(d3dDirectRayGenLibrary, { L"DirectRayGen" });
	pipeline.AddLibrary(d3dIndirectRayGenLibrary, { L"IndirectRayGen" });
	pipeline.AddLibrary(d3dReflectionRayGenLibrary, { L"ReflectionRayGen" });
	pipeline.AddLibrary(d3dRefractionRayGenLibrary, { L"RefractionRayGen" });

	for (Shader *shader : shaders) {
		const auto &surfaceHitGroup = shader->getSurfaceHitGroup();
		const auto &shadowHitGroup = shader->getShadowHitGroup();
		pipeline.AddLibrary(surfaceHitGroup.blob, { surfaceHitGroup.closestHitName, surfaceHitGroup.anyHitName });
		pipeline.AddLibrary(shadowHitGroup.blob, { shadowHitGroup.closestHitName, shadowHitGroup.anyHitName });
	}

	RT64_LOG_PRINTF("Creating root signatures");

	// Create root signatures.
	d3dRayGenSignature = createRayGenSignature();

	RT64_LOG_PRINTF("Adding hit groups");

	// Add the hit groups with the loaded shaders.
	for (Shader *shader : shaders) {
		const auto &surfaceHitGroup = shader->getSurfaceHitGroup();
		const auto &shadowHitGroup = shader->getShadowHitGroup();
		pipeline.AddHitGroup(surfaceHitGroup.hitGroupName, surfaceHitGroup.closestHitName, surfaceHitGroup.anyHitName);
		pipeline.AddHitGroup(shadowHitGroup.hitGroupName, shadowHitGroup.closestHitName, shadowHitGroup.anyHitName);
	}

	RT64_LOG_PRINTF("Adding root signature associations");

	// Associate the root signatures to the hit groups.
	pipeline.AddRootSignatureAssociation(d3dRayGenSignature, { L"PrimaryRayGen", L"DirectRayGen", L"IndirectRayGen", L"ReflectionRayGen", L"RefractionRayGen" });

	for (Shader *shader : shaders) {
		const auto &surfaceHitGroup = shader->getSurfaceHitGroup();
		const auto &shadowHitGroup = shader->getShadowHitGroup();
		pipeline.AddRootSignatureAssociation(surfaceHitGroup.rootSignature, { surfaceHitGroup.hitGroupName });
		pipeline.AddRootSignatureAssociation(shadowHitGroup.rootSignature, { shadowHitGroup.hitGroupName });
	}
	
	// Pipeline configuration. Path tracing only needs one recursion level at most.
	pipeline.SetMaxPayloadSize(2 * sizeof(float));
	pipeline.SetMaxAttributeSize(2 * sizeof(float));
	pipeline.SetMaxRecursionDepth(1);

	RT64_LOG_PRINTF("Generating the pipeline");

	// Generate the pipeline.
	d3dRtStateObject = pipeline.Generate();

	RT64_LOG_PRINTF("Obtaining the object properties");

	// Cast the state object into a properties object, allowing to later access the shader pointers by name.
	D3D12_CHECK(d3dRtStateObject->QueryInterface(IID_PPV_ARGS(&d3dRtStateObjectProps)));

	RT64_LOG_PRINTF("Getting shader identifiers");

	primaryRayGenID = d3dRtStateObjectProps->GetShaderIdentifier(L"PrimaryRayGen");
	directRayGenID = d3dRtStateObjectProps->GetShaderIdentifier(L"DirectRayGen");
	indirectRayGenID = d3dRtStateObjectProps->GetShaderIdentifier(L"IndirectRayGen");
	reflectionRayGenID = d3dRtStateObjectProps->GetShaderIdentifier(L"ReflectionRayGen");
	refractionRayGenID = d3dRtStateObjectProps->GetShaderIdentifier(L"RefractionRayGen");
	surfaceMissID = d3dRtStateObjectProps->GetShaderIdentifier(L"SurfaceMiss");
	shadowMissID = d3dRtStateObjectProps->GetShaderIdentifier(L"ShadowMiss");
	for (Shader *shader : shaders) {
		auto &surfaceHitGroup = shader->getSurfaceHitGroup();
		auto &shadowHitGroup = shader->getShadowHitGroup();
		surfaceHitGroup.id = d3dRtStateObjectProps->GetShaderIdentifier(surfaceHitGroup.hitGroupName.c_str());
		shadowHitGroup.id = d3dRtStateObjectProps->GetShaderIdentifier(shadowHitGroup.hitGroupName.c_str());
	}

	RT64_LOG_PRINTF("Raytracing pipeline creation finished");
}

void RT64::Device::createDxcCompiler() {
	RT64_LOG_PRINTF("Compiler creation started");
	D3D12_CHECK(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void **)&d3dDxcCompiler));
	D3D12_CHECK(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&d3dDxcLibrary));
	RT64_LOG_PRINTF("Compiler creation finished");
}

ID3D12RootSignature *RT64::Device::createRayGenSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;

	// Fill out the heap parameters.
	rsc.AddHeapRangesParameter({
		{ UAV_INDEX(gViewDirection), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gViewDirection) },
		{ UAV_INDEX(gShadingPosition), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gShadingPosition) },
		{ UAV_INDEX(gShadingNormal), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gShadingNormal) },
		{ UAV_INDEX(gShadingSpecular), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gShadingSpecular) },
		{ UAV_INDEX(gDiffuse), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gDiffuse) },
		{ UAV_INDEX(gInstanceId), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gInstanceId) },
		{ UAV_INDEX(gDirectLight), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gDirectLight) },
		{ UAV_INDEX(gIndirectLight), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gIndirectLight) },
		{ UAV_INDEX(gReflection), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gReflection) },
		{ UAV_INDEX(gRefraction), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gRefraction) },
		{ UAV_INDEX(gTransparent), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gTransparent) },
		{ UAV_INDEX(gFlow), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gFlow) },
		{ UAV_INDEX(gDepth), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gDepth) },
		{ UAV_INDEX(gHitDistAndFlow), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitDistAndFlow) },
		{ UAV_INDEX(gHitColor), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitColor) },
		{ UAV_INDEX(gHitNormal), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitNormal) },
		{ UAV_INDEX(gHitSpecular), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitSpecular) },
		{ UAV_INDEX(gHitInstanceId), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, HEAP_INDEX(gHitInstanceId) },
		{ SRV_INDEX(gBackground), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(gBackground) },
		{ SRV_INDEX(SceneBVH), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(SceneBVH) },
		{ SRV_INDEX(SceneLights), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(SceneLights) },
		{ SRV_INDEX(instanceTransforms), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceTransforms) },
		{ SRV_INDEX(instanceMaterials), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(instanceMaterials) },
		{ SRV_INDEX(gTextures), 512, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, HEAP_INDEX(gTextures) },
		{ CBV_INDEX(gParams), 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, HEAP_INDEX(gParams) }
	});

	// Fill out the samplers.
	D3D12_STATIC_SAMPLER_DESC desc;
	desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	desc.MinLOD = 0;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	desc.MipLODBias = 0.0f;
	desc.MaxAnisotropy = 1;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	desc.ShaderRegister = 0;
	desc.RegisterSpace = 0;

	return rsc.Generate(d3dDevice, true, false, &desc, 1);
}

void RT64::Device::preRender() {
	RT64_LOG_PRINTF("Started device prerender");

	// Submit and wait for execution if command list was open.
	if (d3dCommandListOpen) {
		submitCommandList();
		waitForGPU();
	}

	resetCommandList();

	// Set necessary state.
	d3dCommandList->RSSetViewports(1, &d3dViewport);
	d3dCommandList->RSSetScissorRects(1, &d3dScissorRect);

	RT64_LOG_PRINTF("Setting render target");

	// Indicate that the back buffer will be used as a render target.
	CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(d3dRenderTargets[d3dFrameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	d3dCommandList->ResourceBarrier(1, &transitionBarrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = getD3D12RTV();
	d3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	d3dCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	RT64_LOG_PRINTF("Finished device prerender");
}

void RT64::Device::postRender(int vsyncInterval) {
	RT64_LOG_PRINTF("Started device postrender");

	// Indicate that the back buffer will now be used to present.
	CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(d3dRenderTargets[d3dFrameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	d3dCommandList->ResourceBarrier(1, &transitionBarrier);

	submitCommandList();

	// Present the frame.
	D3D12_CHECK(d3dSwapChain->Present(vsyncInterval, 0));

	waitForGPU();
	d3dFrameIndex = d3dSwapChain->GetCurrentBackBufferIndex();

	// Leave command list open.
	resetCommandList();

	RT64_LOG_PRINTF("Finished device postrender");
}

void RT64::Device::draw(int vsyncInterval) {
	RT64_LOG_PRINTF("Started device draw");

	if (d3dRtStateObjectDirty) {
		createRaytracingPipeline();
		d3dRtStateObjectDirty = false;
	}

	submitCommandQueueBarrier();
	submitCopyQueueBarrier();
	
	// Make sure that the size of the window is up to date.
	updateSize();
	
	// Update all scenes as necessary.
	for (Scene *scene : scenes) {
		scene->update();
	}

	// Render each scene.
	preRender();

	for (Scene *scene : scenes) {
		scene->render();
	}

	RT64_LOG_PRINTF("Reset render target");

	// Scene has most likely changed the render target. Set it again for the inspectors to work properly.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = getD3D12RTV();
	d3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Find mouse cursor position.
	POINT cursorPos = {};
	GetCursorPos(&cursorPos);
	ScreenToClient(hwnd, &cursorPos);

	// Determine the active view (use the first available view for now).
	View *activeView = nullptr;
	for (Scene *scene : scenes) {
		auto views = scene->getViews();
		if (!views.empty()) {
			activeView = views[0];
		}
	}

	// Render the inspectors on the active view.
	if (activeView != nullptr) {
		for (Inspector *inspector : inspectors) {
			inspector->render(activeView, cursorPos.x, cursorPos.y);
		}
	}

	postRender(vsyncInterval);

	RT64_LOG_PRINTF("Finished device draw");
}

void RT64::Device::addScene(Scene *scene) {
	assert(scene != nullptr);
	scenes.push_back(scene);
}

void RT64::Device::removeScene(Scene *scene) {
	assert(scene != nullptr);
	scenes.erase(std::remove(scenes.begin(), scenes.end(), scene), scenes.end());
}

void RT64::Device::addShader(Shader *shader) {
	assert(shader != nullptr);
	if (shader->hasHitGroups()) {
		shaders.push_back(shader);
		d3dRtStateObjectDirty = true;
	}
}

void RT64::Device::removeShader(Shader *shader) {
	assert(shader != nullptr);
	if (shader->hasHitGroups()) {
		shaders.erase(std::remove(shaders.begin(), shaders.end(), shader), shaders.end());
		d3dRtStateObjectDirty = true;
	}
}

void RT64::Device::addInspector(Inspector* inspector) {
	assert(inspector != nullptr);
	inspectors.push_back(inspector);
}

void RT64::Device::removeInspector(Inspector* inspector) {
	assert(inspector != nullptr);
	inspectors.erase(std::remove(inspectors.begin(), inspectors.end(), inspector), inspectors.end());
}

void RT64::Device::resetCommandList() {
	RT64_LOG_PRINTF("Command list reset");

	// Reset the command allocator.
	d3dCommandAllocator->Reset();

	// Reset the command list.
	d3dCommandList->Reset(d3dCommandAllocator, nullptr);

	d3dCommandListOpen = true;
}

void RT64::Device::submitCommandList() {
	// Close the command list.
	d3dCommandList->Close();

	// Execute command list and signal on the fence when it's completed.
	ID3D12CommandList *pGraphicsList = { d3dCommandList };
	d3dCommandQueue->ExecuteCommandLists(1, &pGraphicsList);

	d3dCommandListOpen = false;
}

void RT64::Device::waitForGPU() {
	// Schedule a signal command in the queue.
	d3dCommandQueue->Signal(d3dFence, d3dFenceValue);

	// Wait until the fence has been processed.
	d3dFence->SetEventOnCompletion(d3dFenceValue, d3dFenceEvent);
	WaitForSingleObjectEx(d3dFenceEvent, INFINITE, FALSE);

	// Increment the fence value.
	d3dFenceValue++;
}

void RT64::Device::dumpRenderTarget(const std::string &path) {
	ID3D12Resource *renderTarget = getD3D12RenderTarget();

	CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	d3dCommandList->ResourceBarrier(1, &transitionBarrier);

	D3D12_TEXTURE_COPY_LOCATION source = {};
	source.pResource = renderTarget;
	source.SubresourceIndex = 0;
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

	D3D12_SUBRESOURCE_FOOTPRINT subresource = {};
	subresource.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	subresource.Width = width;
	subresource.Height = height;
	subresource.RowPitch = d3dRenderTargetReadbackRowWidth;
	subresource.Depth = 1;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	footprint.Offset = 0;
	footprint.Footprint = subresource;

	D3D12_TEXTURE_COPY_LOCATION destination = {};
	destination.pResource = d3dRenderTargetReadback.Get();
	destination.PlacedFootprint = footprint;
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

	d3dCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

	transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	d3dCommandList->ResourceBarrier(1, &transitionBarrier);

	// Wait until the resource is actually copied.
	submitCommandList();
	waitForGPU();
	resetCommandList();
	
	// Save the render target copy to the target path.
	unsigned char *bmpRGB = (unsigned char *)(malloc(width * height * 3));
	{
		UINT8 *pData;
		d3dRenderTargetReadback.Get()->Map(0, nullptr, reinterpret_cast<void **>(&pData));
		int i = 0;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				bmpRGB[i++] = pData[y * d3dRenderTargetReadbackRowWidth + x * 4 + 0];
				bmpRGB[i++] = pData[y * d3dRenderTargetReadbackRowWidth + x * 4 + 1];
				bmpRGB[i++] = pData[y * d3dRenderTargetReadbackRowWidth + x * 4 + 2];
			}
		}
		d3dRenderTargetReadback.Get()->Unmap(0, nullptr);
	}

	stbi_write_bmp(path.c_str(), width, height, 3, bmpRGB);
	free(bmpRGB);

	// Reset the current render target.
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = getD3D12RTV();
	d3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
}

#endif

// Public

DLLEXPORT RT64_DEVICE *RT64_CreateDevice(void *hwnd) {
	try {
		return (RT64_DEVICE *)(new RT64::Device((HWND)(hwnd)));
	}
	RT64_CATCH_EXCEPTION();
	return nullptr;
}

DLLEXPORT void RT64_DestroyDevice(RT64_DEVICE *devicePtr) {
	assert(devicePtr != nullptr);
	try {
		delete (RT64::Device *)(devicePtr);
	}
	RT64_CATCH_EXCEPTION();
}

#ifndef RT64_MINIMAL

DLLEXPORT void RT64_DrawDevice(RT64_DEVICE *devicePtr, int vsyncInterval) {
	assert(devicePtr != nullptr);
	try {
		RT64::Device *device = (RT64::Device *)(devicePtr);
		device->draw(vsyncInterval);
	}
	RT64_CATCH_EXCEPTION();
}

#endif