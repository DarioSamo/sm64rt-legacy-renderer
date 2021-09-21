//
// RT64
//

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include <map>
#include <set>

#include "rt64_device.h"
#include "rt64_dlss.h"
#include "rt64_instance.h"
#include "rt64_mesh.h"
#include "rt64_scene.h"
#include "rt64_shader.h"
#include "rt64_texture.h"
#include "rt64_view.h"

#include "im3d/im3d.h"
#include "xxhash/xxhash32.h"

#define A_CPU
#include "shaders/ffx_a.h"
#include "shaders/ffx_fsr1.h"

struct FSRConstants {
	XMUINT4 Const0;
	XMUINT4 Const1;
	XMUINT4 Const2;
	XMUINT4 Const3;
};

namespace {
	const int MaxQueries = 16 + 1;
};

// Private

RT64::View::View(Scene *scene) {
	RT64_LOG_PRINTF("Starting view creation");

	assert(scene != nullptr);
	this->scene = scene;
	descriptorHeap = nullptr;
	descriptorHeapEntryCount = 0;
	composeHeap = nullptr;
	samplerHeap = nullptr;
	upscaleHeap = nullptr;
	sharpenHeap = nullptr;
	postProcessHeap = nullptr;
	directFilterHeaps[0] = nullptr;
	directFilterHeaps[1] = nullptr;
	indirectFilterHeaps[0] = nullptr;
	indirectFilterHeaps[1] = nullptr;
	sbtStorageSize = 0;
	activeInstancesBufferTransformsSize = 0;
	activeInstancesBufferMaterialsSize = 0;
	globalParamsBufferData.motionBlurStrength = 0.0f;
	globalParamsBufferData.skyPlaneTexIndex = -1;
	globalParamsBufferData.randomSeed = 0;
	globalParamsBufferData.diSamples = 0;
	globalParamsBufferData.giSamples = 0;
	globalParamsBufferData.maxLightSamples = 12;
	globalParamsBufferData.motionBlurSamples = 32;
	globalParamsBufferData.visualizationMode = 0;
	globalParamsBufferData.frameCount = 0;
	globalParamsBufferSize = 0;
	rtSwap = false;
	rtWidth = 0;
	rtHeight = 0;
	maxReflections = 2;
	rtUpscaleActive = false;
	rtSharpenActive = false;
	rtRecreateBuffers = false;
	rtSkipReprojection = false;
	resolutionScale = 1.0f;
	sharpenAttenuation = 0.25f;
	denoiserEnabled = false;
	rtUpscaleMode = UpscaleMode::Bilinear;
	perspectiveControlActive = false;
	perspectiveCanReproject = true;
	im3dVertexCount = 0;
	rtHitInstanceIdReadbackUpdated = false;
	skyPlaneTexture = nullptr;
	scissorApplied = false;
	viewportApplied = false;

#ifdef RT64_DLSS
	// Try to initialize DLSS. The object will not be initialized if the hardware doesn't support it.
	dlss = new DLSS(scene->getDevice());
	dlssQuality = DLSS::QualityMode::Balanced;
	dlssSharpness = 0.0f;
	dlssAutoExposure = false;
	dlssResolutionOverride = false;
#endif

	createOutputBuffers();
	createGlobalParamsBuffer();
	createUpscalingParamsBuffer();
	createSharpenParamsBuffer();
	createFilterParamsBuffer();

	scene->addView(this);

	RT64_LOG_PRINTF("Finished view creation");
}

RT64::View::~View() {
#ifdef RT64_DLSS
	delete dlss;
#endif
	
	scene->removeView(this);

	releaseOutputBuffers();
}

void RT64::View::createOutputBuffers() {
	RT64_LOG_PRINTF("Starting output buffer creation");

	releaseOutputBuffers();

	outputRtvDescriptorSize = scene->getDevice()->getD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	int screenWidth = scene->getDevice()->getWidth();
	int screenHeight = scene->getDevice()->getHeight();

#ifdef RT64_DLSS
	if ((rtUpscaleMode == UpscaleMode::DLSS) && dlss->isInitialized()) {
		int dlssWidth, dlssHeight;
		DLSS::QualityMode setQuality = DLSS::QualityMode::Balanced;
		float unusedSharpness;
		if (dlssResolutionOverride) {
			rtWidth = lround(screenWidth * resolutionScale);
			rtHeight = lround(screenHeight * resolutionScale);
		}
		else if (dlss->getQualityInformation(dlssQuality, screenWidth, screenHeight, dlssWidth, dlssHeight, unusedSharpness)) {
			rtWidth = dlssWidth;
			rtHeight = dlssHeight;
			setQuality = dlssQuality;
		}
		else {
			rtWidth = screenWidth;
			rtHeight = screenHeight;
		}

		dlss->set(setQuality, rtWidth, rtHeight, screenWidth, screenHeight, dlssAutoExposure);

		rtUpscaleActive = true;
		rtSharpenActive = false;
	}
	else
#endif
	if (rtUpscaleMode == UpscaleMode::FSR) {
		rtUpscaleActive = true;
		rtSharpenActive = true;
	}
	else
	{
		rtWidth = lround(screenWidth * resolutionScale);
		rtHeight = lround(screenHeight * resolutionScale);
		rtUpscaleActive = false;
		rtSharpenActive = false;
	}

	globalParamsBufferData.resolution.x = (float)(rtWidth);
	globalParamsBufferData.resolution.y = (float)(rtHeight);
	globalParamsBufferData.resolution.z = (float)(screenWidth);
	globalParamsBufferData.resolution.w = (float)(screenHeight);

	fprintf(stdout, "Render buffer: %dX%d\n", rtWidth, rtHeight);

	D3D12_CLEAR_VALUE clearValue = { };
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;
	clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D12_RESOURCE_DESC resDesc = { };
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	resDesc.Width = screenWidth;
	resDesc.Height = screenHeight;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;

	// Create buffers for raster output.
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	rasterBg = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clearValue);

	// Create buffers for raytracing output.
	resDesc.Width = rtWidth;
	resDesc.Height = rtHeight;
	resDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	rtOutput[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	rtOutput[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	rtShadingPosition = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtDiffuse = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rtNormal[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtNormal[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtShadingNormal = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);

	resDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	rtFlow = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Format = DXGI_FORMAT_R32_FLOAT;
	rtDepth[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	rtDepth[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	rtViewDirection = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	rtShadingSpecular = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtDirectLightAccum[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtDirectLightAccum[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtIndirectLightAccum[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtIndirectLightAccum[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtFilteredDirectLight[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtFilteredDirectLight[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtFilteredIndirectLight[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtFilteredIndirectLight[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
	rtReflection = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	rtRefraction = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	rtTransparent = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Format = DXGI_FORMAT_R32_SINT; // TODO: To optimize to UINT, we need to insert an empty instance at the start and use 0 as the invalid value instead of -1.
	rtInstanceId = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);

	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	if (rtUpscaleActive) {
		resDesc.Width = screenWidth;
		resDesc.Height = screenHeight;
		rtOutputUpscaled = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
		rtOutputSharpened = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	}
	else {
		rtOutputSharpened = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
	}

	// Create hit result buffers.
	UINT64 hitCountBufferSizeOne = rtWidth * rtHeight;
	UINT64 hitCountBufferSizeAll = hitCountBufferSizeOne * MaxQueries;
	rtHitDistAndFlow = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitColor = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitNormal = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 8, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitSpecular = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitInstanceId = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitInstanceIdReadback = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_READBACK, hitCountBufferSizeOne * 2, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

#ifndef NDEBUG
	rasterBg.SetName(L"rasterBg");
	rtOutput[0].SetName(L"rtOutput[0]");
	rtOutput[1].SetName(L"rtOutput[1]");
	rtViewDirection.SetName(L"rtViewDirection");
	rtShadingPosition.SetName(L"rtShadingPosition");
	rtShadingNormal.SetName(L"rtShadingNormal");
	rtShadingSpecular.SetName(L"rtShadingSpecular");
	rtDiffuse.SetName(L"rtDiffuse");
	rtNormal[0].SetName(L"rtNormal[0]");
	rtNormal[1].SetName(L"rtNormal[1]");
	rtInstanceId.SetName(L"rtInstanceId");
	rtDirectLightAccum[0].SetName(L"rtDirectLightAccum[0]");
	rtDirectLightAccum[1].SetName(L"rtDirectLightAccum[1]");
	rtIndirectLightAccum[0].SetName(L"rtIndirectLightAccum[0]");
	rtIndirectLightAccum[1].SetName(L"rtIndirectLightAccum[1]");
	rtFilteredDirectLight[0].SetName(L"rtFilteredDirectLight[0]");
	rtFilteredDirectLight[1].SetName(L"rtFilteredDirectLight[1]");
	rtFilteredIndirectLight[0].SetName(L"rtFilteredIndirectLight[0]");
	rtFilteredIndirectLight[1].SetName(L"rtFilteredIndirectLight[1]");
	rtReflection.SetName(L"rtReflection");
	rtRefraction.SetName(L"rtRefraction");
	rtTransparent.SetName(L"rtTransparent");
	rtFlow.SetName(L"rtFlow");
	rtDepth[0].SetName(L"rtDepth[0]");
	rtDepth[1].SetName(L"rtDepth[1]");
	rtHitDistAndFlow.SetName(L"rtHitDistAndFlow");
	rtHitColor.SetName(L"rtHitColor");
	rtHitNormal.SetName(L"rtHitNormal");
	rtHitSpecular.SetName(L"rtHitSpecular");
	rtHitInstanceId.SetName(L"rtHitInstanceId");
	rtHitInstanceIdReadback.SetName(L"rtHitInstanceIdReadback");
	rtOutputUpscaled.SetName(L"rtOutputUpscaled");
	rtOutputSharpened.SetName(L"rtOutputSharpened");
#endif

	// Create the RTVs.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	D3D12_CHECK(scene->getDevice()->getD3D12Device()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rasterBgHeap)));
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvBgHandle(rasterBgHeap->GetCPUDescriptorHandleForHeapStart());
	scene->getDevice()->getD3D12Device()->CreateRenderTargetView(rasterBg.Get(), nullptr, rtvBgHandle);

	for (int i = 0; i < 2; i++) {
		D3D12_CHECK(scene->getDevice()->getD3D12Device()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&outputBgHeap[i])));
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvOutHandle(outputBgHeap[i]->GetCPUDescriptorHandleForHeapStart());
		scene->getDevice()->getD3D12Device()->CreateRenderTargetView(rtOutput[i].Get(), nullptr, rtvOutHandle);
	}

	RT64_LOG_PRINTF("Finished output buffer creation");
}

void RT64::View::releaseOutputBuffers() {
	rasterBg.Release();
	rtOutput[0].Release();
	rtOutput[1].Release();
	rtViewDirection.Release();
	rtShadingPosition.Release();
	rtShadingNormal.Release();
	rtShadingSpecular.Release();
	rtDiffuse.Release();
	rtNormal[0].Release();
	rtNormal[1].Release();
	rtInstanceId.Release();
	rtDirectLightAccum[0].Release();
	rtDirectLightAccum[1].Release();
	rtIndirectLightAccum[0].Release();
	rtIndirectLightAccum[1].Release();
	rtFilteredDirectLight[0].Release();
	rtFilteredDirectLight[1].Release();
	rtFilteredIndirectLight[0].Release();
	rtFilteredIndirectLight[1].Release();
	rtReflection.Release();
	rtRefraction.Release();
	rtTransparent.Release();
	rtFlow.Release();
	rtDepth[0].Release();
	rtDepth[1].Release();
	rtHitDistAndFlow.Release();
	rtHitColor.Release();
	rtHitNormal.Release();
	rtHitSpecular.Release();
	rtHitInstanceId.Release();
	rtHitInstanceIdReadback.Release();
	rtOutputUpscaled.Release();
	rtOutputSharpened.Release();
}

void RT64::View::createInstanceTransformsBuffer() {
	uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
	uint32_t newBufferSize = ROUND_UP(totalInstances * sizeof(InstanceTransforms), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	if (activeInstancesBufferTransformsSize != newBufferSize) {
		activeInstancesBufferTransforms.Release();
		activeInstancesBufferTransforms = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, newBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
		activeInstancesBufferTransformsSize = newBufferSize;
	}
}

void RT64::View::updateInstanceTransformsBuffer() {
	InstanceTransforms *current = nullptr;
	CD3DX12_RANGE readRange(0, 0);

	D3D12_CHECK(activeInstancesBufferTransforms.Get()->Map(0, &readRange, reinterpret_cast<void **>(&current)));

	for (const RenderInstance &inst : rtInstances) {
		// Store world transform.
		current->objectToWorld = inst.transform;
		current->objectToWorldPrevious = inst.transformPrevious;

		// Store matrix to transform normal.
		XMMATRIX upper3x3 = current->objectToWorld;
		upper3x3.r[0].m128_f32[3] = 0.f;
		upper3x3.r[1].m128_f32[3] = 0.f;
		upper3x3.r[2].m128_f32[3] = 0.f;
		upper3x3.r[3].m128_f32[0] = 0.f;
		upper3x3.r[3].m128_f32[1] = 0.f;
		upper3x3.r[3].m128_f32[2] = 0.f;
		upper3x3.r[3].m128_f32[3] = 1.f;

		XMVECTOR det;
		current->objectToWorldNormal = XMMatrixTranspose(XMMatrixInverse(&det, upper3x3));

		current++;
	}

	activeInstancesBufferTransforms.Get()->Unmap(0, nullptr);
}

void RT64::View::createInstanceMaterialsBuffer() {
	uint32_t totalInstances = static_cast<uint32_t>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
	uint32_t newBufferSize = ROUND_UP(totalInstances * sizeof(RT64_MATERIAL), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	if (activeInstancesBufferMaterialsSize != newBufferSize) {
		activeInstancesBufferMaterials.Release();
		activeInstancesBufferMaterials = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, newBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
		activeInstancesBufferMaterialsSize = newBufferSize;
	}
}

void RT64::View::updateInstanceMaterialsBuffer() {
	RT64_MATERIAL *current = nullptr;
	CD3DX12_RANGE readRange(0, 0);

	D3D12_CHECK(activeInstancesBufferMaterials.Get()->Map(0, &readRange, reinterpret_cast<void **>(&current)));

	for (const RenderInstance &inst : rtInstances) {
		*current = inst.material;
		current++;
	}

	for (const RenderInstance &inst : rasterBgInstances) {
		*current = inst.material;
		current++;
	}

	for (const RenderInstance& inst : rasterFgInstances) {
		*current = inst.material;
		current++;
	}

	activeInstancesBufferMaterials.Get()->Unmap(0, nullptr);
}

void RT64::View::createTopLevelAS(const std::vector<RenderInstance>& rtInstances) {
	// Reset the generator.
	topLevelASGenerator.Reset();

	// Gather all the instances into the builder helper
	for (size_t i = 0; i < rtInstances.size(); i++) {
		topLevelASGenerator.AddInstance(rtInstances[i].bottomLevelAS, rtInstances[i].transform, static_cast<UINT>(i), static_cast<UINT>(2 * i), rtInstances[i].flags);
	}

	// As for the bottom-level AS, the building the AS requires some scratch
	// space to store temporary data in addition to the actual AS. In the case
	// of the top-level AS, the instance descriptors also need to be stored in
	// GPU memory. This call outputs the memory requirements for each (scratch,
	// results, instance descriptors) so that the application can allocate the
	// corresponding memory
	UINT64 scratchSize, resultSize, instanceDescsSize;
	topLevelASGenerator.ComputeASBufferSizes(scene->getDevice()->getD3D12Device(), true, &scratchSize, &resultSize, &instanceDescsSize);
	
	// Release the previous buffers and reallocate them if they're not big enough.
	if ((topLevelASBuffers.scratchSize < scratchSize) || (topLevelASBuffers.resultSize < resultSize) || (topLevelASBuffers.instanceDescSize < instanceDescsSize)) {
		topLevelASBuffers.Release();

		// Create the scratch and result buffers. Since the build is all done on
		// GPU, those can be allocated on the default heap
		topLevelASBuffers.scratch = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		topLevelASBuffers.result = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

		// The buffer describing the instances: ID, shader binding information,
		// matrices ... Those will be copied into the buffer by the helper through
		// mapping, so the buffer has to be allocated on the upload heap.
		topLevelASBuffers.instanceDesc = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, instanceDescsSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);

		topLevelASBuffers.scratchSize = scratchSize;
		topLevelASBuffers.resultSize = resultSize;
		topLevelASBuffers.instanceDescSize = instanceDescsSize;
	}

	// After all the buffers are allocated, or if only an update is required, we can build the acceleration structure. 
	// Note that in the case of the update we also pass the existing AS as the 'previous' AS, so that it can be refitted in place.
	topLevelASGenerator.Generate(scene->getDevice()->getD3D12CommandList(), topLevelASBuffers.scratch.Get(), topLevelASBuffers.result.Get(), topLevelASBuffers.instanceDesc.Get(), false, topLevelASBuffers.result.Get());
}

void RT64::View::createShaderResourceHeap() {
	assert(usedTextures.size() <= SRV_TEXTURES_MAX);

	const UINT handleIncrement = scene->getDevice()->getD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	{
		uint32_t entryCount = ((uint32_t)(HeapIndices::MAX)-1) + SRV_TEXTURES_MAX;

		// Recreate descriptor heap to be bigger if necessary.
		bool fillWithNull = false;
		if (descriptorHeapEntryCount < entryCount) {
			if (descriptorHeap != nullptr) {
				descriptorHeap->Release();
				descriptorHeap = nullptr;
			}

			descriptorHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), entryCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
			descriptorHeapEntryCount = entryCount;
			fillWithNull = true;
		}

		// Get a handle to the heap memory on the CPU side, to be able to write the
		// descriptors directly
		D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

		// UAV for view direction buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtViewDirection.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for shading position buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtShadingPosition.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for shading normal buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtShadingNormal.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for shading specular buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtShadingSpecular.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for diffuse buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtDiffuse.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for instance ID buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtInstanceId.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for direct light buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtDirectLightAccum[rtSwap ? 1 : 0].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for indirect light buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtIndirectLightAccum[rtSwap ? 1 : 0].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for reflection buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtReflection.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for refraction buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtRefraction.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for transparent buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtTransparent.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;
		
		// UAV for flow buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtFlow.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for first hit normal buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtNormal[rtSwap ? 1 : 0].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for depth buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtDepth[rtSwap ? 1 : 0].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for previous first hit normal buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtNormal[rtSwap ? 0 : 1].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for previous depth buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtDepth[rtSwap ? 0 : 1].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for previous direct light buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtDirectLightAccum[rtSwap ? 0 : 1].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for previous indirect light buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtIndirectLightAccum[rtSwap ? 0 : 1].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for filtered direct light buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtFilteredDirectLight[1].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for filtered indirect light buffer.
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtFilteredIndirectLight[1].Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for hit distance and world flow buffer.
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = rtWidth * rtHeight * MaxQueries;
		uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtHitDistAndFlow.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for hit color buffer.
		uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtHitColor.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for hit normal buffer.
		uavDesc.Format = DXGI_FORMAT_R16G16B16A16_SNORM;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtHitNormal.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for hit specular buffer.
		uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtHitSpecular.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for hit shading buffer.
		uavDesc.Format = DXGI_FORMAT_R16_UINT;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtHitInstanceId.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for background texture.
		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = 1;
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rasterBg.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// Describe and create a constant buffer view for the global parameters.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = globalParamBufferResource.Get()->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = globalParamsBufferSize;
		scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
		handle.ptr += handleIncrement;

		// Add the Top Level AS SRV.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		if (!topLevelASBuffers.result.IsNull()) {
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.RaytracingAccelerationStructure.Location = topLevelASBuffers.result.Get()->GetGPUVirtualAddress();
			scene->getDevice()->getD3D12Device()->CreateShaderResourceView(nullptr, &srvDesc, handle);
		}

		handle.ptr += handleIncrement;

		// Describe and create a constant buffer view for the lights.
		if (scene->getLightsCount() > 0) {
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = scene->getLightsCount();
			srvDesc.Buffer.StructureByteStride = sizeof(RT64_LIGHT);
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			scene->getDevice()->getD3D12Device()->CreateShaderResourceView(scene->getLightsBuffer(), &srvDesc, handle);
		}

		handle.ptr += handleIncrement;

		// Describe the transforms buffer per instance.
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
		srvDesc.Buffer.StructureByteStride = sizeof(InstanceTransforms);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(activeInstancesBufferTransforms.Get(), &srvDesc, handle);
		handle.ptr += handleIncrement;

		// Describe the properties buffer per instance.
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(rtInstances.size() + rasterBgInstances.size() + rasterFgInstances.size());
		srvDesc.Buffer.StructureByteStride = sizeof(RT64_MATERIAL);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(activeInstancesBufferMaterials.Get(), &srvDesc, handle);
		handle.ptr += handleIncrement;

		// Add the blue noise SRV.
		Texture *blueNoiseTexture = scene->getDevice()->getBlueNoiseTexture();
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Texture2D.MipLevels = -1;
		textureSRVDesc.Format = blueNoiseTexture->getFormat();
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(blueNoiseTexture->getTexture(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// Add the texture SRVs.
		for (size_t i = 0; i < usedTextures.size(); i++) {
			textureSRVDesc.Format = usedTextures[i]->getFormat();
			scene->getDevice()->getD3D12Device()->CreateShaderResourceView(usedTextures[i]->getTexture(), &textureSRVDesc, handle);
			usedTextures[i]->setCurrentIndex(-1);
			handle.ptr += handleIncrement;
		}

		// Fill with null SRVs if the heap was just created.
		if (fillWithNull) {
			for (size_t i = usedTextures.size(); i < SRV_TEXTURES_MAX; i++) {
				scene->getDevice()->getD3D12Device()->CreateShaderResourceView(nullptr, &textureSRVDesc, handle);
				handle.ptr += handleIncrement;
			}
		}
	}

	{
		// Create the heap for the samplers.
		// Unlike the others, this one only needs to be created once.
		// TODO: Maybe move this to initialization instead.
		if (samplerHeap == nullptr) {
			const UINT samplerHandleIncrement = scene->getDevice()->getD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			uint32_t handleCount = 18;
			samplerHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);

			D3D12_CPU_DESCRIPTOR_HANDLE handle = samplerHeap->GetCPUDescriptorHandleForHeapStart();

			// Add the texture samplers.
			D3D12_SAMPLER_DESC samplerDesc;
			samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplerDesc.MinLOD = 0;
			samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			samplerDesc.MipLODBias = 0.0f;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			samplerDesc.BorderColor[0] = 0.0f;
			samplerDesc.BorderColor[1] = 0.0f;
			samplerDesc.BorderColor[2] = 0.0f;
			samplerDesc.BorderColor[3] = 0.0f;

			for (int filter = 0; filter < 2; filter++) {
				samplerDesc.Filter = filter ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
				for (int hAddr = 0; hAddr < 3; hAddr++) {
					for (int vAddr = 0; vAddr < 3; vAddr++) {
						samplerDesc.AddressU = (hAddr == 2) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : (hAddr == 1) ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
						samplerDesc.AddressV = (vAddr == 2) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : (vAddr == 1) ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
						scene->getDevice()->getD3D12Device()->CreateSampler(&samplerDesc, handle);
						handle.ptr += samplerHandleIncrement;
					}
				}
			}
		}
	}

	{
		// Create the heap for the compose shader.
		if (composeHeap == nullptr) {
			uint32_t handleCount = 8;
			composeHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = composeHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = 1;
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// SRV for motion vector texture.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFlow.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for diffuse buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtDiffuse.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for direct light buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFilteredDirectLight[1].Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for filtered indirect light buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFilteredIndirectLight[1].Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for reflection buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtReflection.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for refraction buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtRefraction.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for transparent buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtTransparent.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// CBV for global parameters.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = globalParamBufferResource.Get()->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = globalParamsBufferSize;
		scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
		handle.ptr += handleIncrement;
	}

	if (rtUpscaleActive) {
		// Create the heap for upscaling.
		if (upscaleHeap == nullptr) {
			uint32_t handleCount = 3;
			upscaleHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = upscaleHeap->GetCPUDescriptorHandleForHeapStart();

		// SRV for input image.
		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = 1;
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtOutput[rtSwap ? 1 : 0].Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for output image.
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtOutputUpscaled.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// CBV for upscaling parameters.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = upscalingParamBufferResource.Get()->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = upscalingParamBufferSize;
		scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
		handle.ptr += handleIncrement;
	}

	if (rtSharpenActive) {
		// Create the heap for sharpen.
		if (sharpenHeap == nullptr) {
			uint32_t handleCount = 3;
			sharpenHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = sharpenHeap->GetCPUDescriptorHandleForHeapStart();

		// SRV for input image.
		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = 1;
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		ID3D12Resource *inputResource = nullptr;
		if (rtUpscaleActive) {
			inputResource = rtOutputUpscaled.Get();
			textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}
		else {
			inputResource = rtOutput[rtSwap ? 1 : 0].Get();
			textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		}

		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(inputResource, &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// UAV for output image.
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtOutputSharpened.Get(), nullptr, &uavDesc, handle);
		handle.ptr += handleIncrement;

		// CBV for sharpen parameters.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = sharpenParamBufferResource.Get()->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = sharpenParamBufferSize;
		scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
		handle.ptr += handleIncrement;
	}

	{
		// Create the heap for the post process shader.
		if (postProcessHeap == nullptr) {
			uint32_t handleCount = 3;
			postProcessHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = postProcessHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = 1;
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// SRV for input image.
		ID3D12Resource *inputResource = nullptr;
		if (rtSharpenActive) {
			inputResource = rtOutputSharpened.Get();
			textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}
		else if (rtUpscaleActive) {
			inputResource = rtOutputUpscaled.Get();
			textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}
		else {
			inputResource = rtOutput[rtSwap ? 1 : 0].Get();
			textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		}

		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(inputResource, &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for flow buffer.
		textureSRVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFlow.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// CBV for global parameters.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = globalParamBufferResource.Get()->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = globalParamsBufferSize;
		scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
		handle.ptr += handleIncrement;
	}

	{
		// Create the heap for direct filter.
		for (int i = 0; i < 2; i++) {
			if (directFilterHeaps[i] == nullptr) {
				uint32_t handleCount = 3;
				directFilterHeaps[i] = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
			}

			D3D12_CPU_DESCRIPTOR_HANDLE handle = directFilterHeaps[i]->GetCPUDescriptorHandleForHeapStart();

			// SRV for input image.
			D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
			textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			textureSRVDesc.Texture2D.MipLevels = 1;
			textureSRVDesc.Texture2D.MostDetailedMip = 0;
			textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFilteredDirectLight[i ? 1 : 0].Get(), &textureSRVDesc, handle);
			handle.ptr += handleIncrement;

			// UAV for output image.
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtFilteredDirectLight[i ? 0 : 1].Get(), nullptr, &uavDesc, handle);
			handle.ptr += handleIncrement;

			// CBV for sharpen parameters.
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = filterParamBufferResource.Get()->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = filterParamBufferSize;
			scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
			handle.ptr += handleIncrement;
		}
	}

	{
		// Create the heap for indirect filter.
		for (int i = 0; i < 2; i++) {
			if (indirectFilterHeaps[i] == nullptr) {
				uint32_t handleCount = 3;
				indirectFilterHeaps[i] = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
			}

			D3D12_CPU_DESCRIPTOR_HANDLE handle = indirectFilterHeaps[i]->GetCPUDescriptorHandleForHeapStart();

			// SRV for input image.
			D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
			textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			textureSRVDesc.Texture2D.MipLevels = 1;
			textureSRVDesc.Texture2D.MostDetailedMip = 0;
			textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			textureSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFilteredIndirectLight[i ? 1 : 0].Get(), &textureSRVDesc, handle);
			handle.ptr += handleIncrement;

			// UAV for output image.
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtFilteredIndirectLight[i ? 0 : 1].Get(), nullptr, &uavDesc, handle);
			handle.ptr += handleIncrement;

			// CBV for sharpen parameters.
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = filterParamBufferResource.Get()->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = filterParamBufferSize;
			scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
			handle.ptr += handleIncrement;
		}
	}
}

void RT64::View::createShaderBindingTable() {
	// The SBT helper class collects calls to Add*Program. If called several times, the helper must be emptied before re-adding shaders.
	sbtHelper.Reset();

	// The pointer to the beginning of the heap is the only parameter required by shaders without root parameters
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapHandle = samplerHeap->GetGPUDescriptorHandleForHeapStart();
	
	// The helper treats both root parameter pointers and heap pointers as void*, while DX12 uses the D3D12_GPU_DESCRIPTOR_HANDLE 
	// to define heap pointers. The pointer in this struct is a UINT64, which then has to be reinterpreted as a pointer.
	auto srvUavPointer = reinterpret_cast<UINT64 *>(srvUavHeapHandle.ptr);
	auto samplerPointer = reinterpret_cast<UINT64 *>(samplerHeapHandle.ptr);

	// The ray generation only uses heap data.
	sbtHelper.AddRayGenerationProgram(scene->getDevice()->getPrimaryRayGenID(), { srvUavPointer });
	sbtHelper.AddRayGenerationProgram(scene->getDevice()->getDirectRayGenID(), { srvUavPointer });
	sbtHelper.AddRayGenerationProgram(scene->getDevice()->getIndirectRayGenID(), { srvUavPointer });
	sbtHelper.AddRayGenerationProgram(scene->getDevice()->getReflectionRayGenID(), { srvUavPointer });
	sbtHelper.AddRayGenerationProgram(scene->getDevice()->getRefractionRayGenID(), { srvUavPointer });
	
	// Miss shaders don't use any external data.
	sbtHelper.AddMissProgram(scene->getDevice()->getSurfaceMissID(), {});
	sbtHelper.AddMissProgram(scene->getDevice()->getShadowMissID(), {});

	// Add the vertex buffers from all the meshes used by the instances to the hit group.
	for (const RenderInstance &rtInstance : rtInstances) {
		const auto &surfaceHitGroup = rtInstance.shader->getSurfaceHitGroup();
		sbtHelper.AddHitGroup(surfaceHitGroup.id, {
			(void *)(rtInstance.vertexBufferView->BufferLocation),
			(void *)(rtInstance.indexBufferView->BufferLocation),
			srvUavPointer,
			samplerPointer
		});
		
		const auto &shadowHitGroup = rtInstance.shader->getShadowHitGroup();
		sbtHelper.AddHitGroup(shadowHitGroup.id, {
			(void*)(rtInstance.vertexBufferView->BufferLocation),
			(void*)(rtInstance.indexBufferView->BufferLocation),
			srvUavPointer,
			samplerPointer
		});
	}
	
	// Compute the size of the SBT given the number of shaders and their parameters.
	uint32_t sbtSize = sbtHelper.ComputeSBTSize();
	if (sbtStorageSize < sbtSize) {
		// Release previously allocated SBT storage.
		sbtStorage.Release();

		// Create the SBT on the upload heap. This is required as the helper will use
		// mapping to write the SBT contents. After the SBT compilation it could be
		// copied to the default heap for performance.
		sbtStorage = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, sbtSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
		sbtStorageSize = sbtSize;
	}

	// Compile the SBT from the shader and parameters info
	sbtHelper.Generate(sbtStorage.Get(), scene->getDevice()->getD3D12RtStateObjectProperties());
}

void RT64::View::createGlobalParamsBuffer() {
	globalParamsBufferSize = ROUND_UP(sizeof(GlobalParamsBuffer), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	globalParamBufferResource = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, globalParamsBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void RT64::View::updateGlobalParamsBuffer() {
	assert(fovRadians > 0.0f);

	// Update with the latest scene description.
	RT64_SCENE_DESC desc = scene->getDescription();
	globalParamsBufferData.ambientBaseColor = ToVector4(desc.ambientBaseColor, 0.0f);
	globalParamsBufferData.ambientNoGIColor = ToVector4(desc.ambientNoGIColor, 0.0f);
	globalParamsBufferData.eyeLightDiffuseColor = ToVector4(desc.eyeLightDiffuseColor, 0.0f);
	globalParamsBufferData.eyeLightSpecularColor = ToVector4(desc.eyeLightSpecularColor, 0.0f);
	globalParamsBufferData.skyDiffuseMultiplier = ToVector4(desc.skyDiffuseMultiplier, 0.0f);
	globalParamsBufferData.skyHSLModifier = ToVector4(desc.skyHSLModifier, 0.0f);
	globalParamsBufferData.skyYawOffset = desc.skyYawOffset;
	globalParamsBufferData.giDiffuseStrength = desc.giDiffuseStrength;
	globalParamsBufferData.giSkyStrength = desc.giSkyStrength;

	// Previous and current view and projection matrices and their inverse.
	if (perspectiveCanReproject) {
		globalParamsBufferData.prevViewI = globalParamsBufferData.viewI;
		globalParamsBufferData.prevViewProj = globalParamsBufferData.viewProj;
	}

	XMVECTOR det;
	globalParamsBufferData.viewI = XMMatrixInverse(&det, globalParamsBufferData.view);
	globalParamsBufferData.projectionI = XMMatrixInverse(&det, globalParamsBufferData.projection);
	globalParamsBufferData.viewProj = XMMatrixMultiply(globalParamsBufferData.view, globalParamsBufferData.projection);

	if (!perspectiveCanReproject) {
		globalParamsBufferData.prevViewI = globalParamsBufferData.viewI;
		globalParamsBufferData.prevViewProj = globalParamsBufferData.viewProj;
	}

	// Pinhole camera vectors to generate non-normalized ray direction.
	// TODO: Make a fake target and focal distance at the midpoint of the near/far planes
	// until the game sends that data in some way in the future.
	const float FocalDistance = (nearDist + farDist) / 2.0f;
	const float AspectRatio = scene->getDevice()->getAspectRatio();
	const RT64_VECTOR3 Up = { 0.0f, 1.0f, 0.0f };
	const RT64_VECTOR3 Pos = getViewPosition();
	const RT64_VECTOR3 Target = Pos + getViewDirection() * FocalDistance;
	RT64_VECTOR3 cameraW = Normalize(Target - Pos) * FocalDistance;
	RT64_VECTOR3 cameraU = Normalize(Cross(cameraW, Up));
	RT64_VECTOR3 cameraV = Normalize(Cross(cameraU, cameraW));
	const float ulen = FocalDistance * std::tan(fovRadians * 0.5f) * AspectRatio;
	const float vlen = FocalDistance * std::tan(fovRadians * 0.5f);
	cameraU = cameraU * ulen;
	cameraV = cameraV * vlen;
	globalParamsBufferData.cameraU = ToVector4(cameraU, 0.0f);
	globalParamsBufferData.cameraV = ToVector4(cameraV, 0.0f);
	globalParamsBufferData.cameraW = ToVector4(cameraW, 0.0f);

	// Enable light reprojection if denoising is enabled.
#ifdef DI_REPROJECTION_SUPPORT
	globalParamsBufferData.diReproject = !rtSkipReprojection && denoiserEnabled && (globalParamsBufferData.diSamples > 0) ? 1 : 0;
#else
	globalParamsBufferData.diReproject = 0;
#endif
	globalParamsBufferData.giReproject = !rtSkipReprojection && denoiserEnabled && (globalParamsBufferData.giSamples > 0) ? 1 : 0;

	// Use the total frame count as the random seed.
	globalParamsBufferData.randomSeed = globalParamsBufferData.frameCount;
	
	// Copy the camera buffer data to the resource.
	uint8_t *pData;
	D3D12_CHECK(globalParamBufferResource.Get()->Map(0, nullptr, (void **)&pData));
	memcpy(pData, &globalParamsBufferData, sizeof(GlobalParamsBuffer));
	globalParamBufferResource.Get()->Unmap(0, nullptr);
}

void RT64::View::createUpscalingParamsBuffer() {
	upscalingParamBufferSize = ROUND_UP(sizeof(FSRConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	upscalingParamBufferResource = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, upscalingParamBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void RT64::View::updateUpscalingParamsBuffer() {
	FSRConstants consts = {};
	FsrEasuCon(
		reinterpret_cast<AU1 *>(&consts.Const0),
		reinterpret_cast<AU1 *>(&consts.Const1),
		reinterpret_cast<AU1 *>(&consts.Const2), 
		reinterpret_cast<AU1 *>(&consts.Const3), 
		static_cast<AF1>(rtWidth), 
		static_cast<AF1>(rtHeight), 
		static_cast<AF1>(rtWidth), 
		static_cast<AF1>(rtHeight), 
		(AF1)(scene->getDevice()->getWidth()), 
		(AF1)(scene->getDevice()->getHeight())
	);

	uint8_t *pData;
	D3D12_CHECK(upscalingParamBufferResource.Get()->Map(0, nullptr, (void **)&pData));
	memcpy(pData, &consts, sizeof(FSRConstants));
	upscalingParamBufferResource.Get()->Unmap(0, nullptr);
}

void RT64::View::createSharpenParamsBuffer() {
	sharpenParamBufferSize = ROUND_UP(sizeof(FSRConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	sharpenParamBufferResource = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, sharpenParamBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void RT64::View::updateSharpenParamsBuffer() {
	FSRConstants consts = {};
	FsrRcasCon(reinterpret_cast<AU1 *>(&consts.Const0), sharpenAttenuation);

	uint8_t *pData;
	D3D12_CHECK(sharpenParamBufferResource.Get()->Map(0, nullptr, (void **)&pData));
	memcpy(pData, &consts, sizeof(FSRConstants));
	sharpenParamBufferResource.Get()->Unmap(0, nullptr);
}

struct alignas(16) FilterCB {
	uint32_t TextureSize[2];
	DirectX::XMFLOAT2 TexelSize;
};

void RT64::View::createFilterParamsBuffer() {
	filterParamBufferSize = ROUND_UP(sizeof(FilterCB), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	filterParamBufferResource = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_UPLOAD, filterParamBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void RT64::View::updateFilterParamsBuffer() {
	FilterCB cb;
	cb.TextureSize[0] = rtWidth;
	cb.TextureSize[1] = rtHeight;
	cb.TexelSize.x = 1.0f / cb.TextureSize[0];
	cb.TexelSize.y = 1.0f / cb.TextureSize[1];

	uint8_t *pData;
	D3D12_CHECK(filterParamBufferResource.Get()->Map(0, nullptr, (void **)&pData));
	memcpy(pData, &cb, sizeof(FilterCB));
	filterParamBufferResource.Get()->Unmap(0, nullptr);
}

void RT64::View::update() {
	RT64_LOG_PRINTF("Started view update");

	// Recreate buffers if necessary for next frame.
	if (rtRecreateBuffers) {
		createOutputBuffers();
		rtSkipReprojection = true;
		rtRecreateBuffers = false;
	}

	auto getTextureIndex = [this](Texture *texture) {
		if (texture == nullptr) {
			return -1;
		}

		int currentIndex = texture->getCurrentIndex();
		if (currentIndex < 0) {
			currentIndex = (int)(usedTextures.size());
			texture->setCurrentIndex(currentIndex);
			usedTextures.push_back(texture);
		}

		return currentIndex;
	};

	usedTextures.clear();
	usedTextures.reserve(SRV_TEXTURES_MAX);
	globalParamsBufferData.skyPlaneTexIndex = getTextureIndex(skyPlaneTexture);

	if (!scene->getInstances().empty()) {
		// Create the active instance vectors.
		RenderInstance renderInstance;
		Mesh* usedMesh = nullptr;
		Texture *usedDiffuse = nullptr;
		size_t totalInstances = scene->getInstances().size();
		unsigned int instFlags = 0;
		unsigned int screenHeight = getHeight();
		rtInstances.clear();
		rasterBgInstances.clear();
		rasterFgInstances.clear();

		rtInstances.reserve(totalInstances);
		rasterBgInstances.reserve(totalInstances);
		rasterFgInstances.reserve(totalInstances);

		for (Instance *instance : scene->getInstances()) {
			instFlags = instance->getFlags();
			usedMesh = instance->getMesh();
			renderInstance.instance = instance;
			renderInstance.bottomLevelAS = usedMesh->getBottomLevelASResult();
			renderInstance.transform = instance->getTransform();
			renderInstance.transformPrevious = instance->getPreviousTransform();
			renderInstance.material = instance->getMaterial();
			renderInstance.shader = instance->getShader();
			renderInstance.indexCount = usedMesh->getIndexCount();
			renderInstance.indexBufferView = usedMesh->getIndexBufferView();
			renderInstance.vertexBufferView = usedMesh->getVertexBufferView();
			renderInstance.flags = (instFlags & RT64_INSTANCE_DISABLE_BACKFACE_CULLING) ? D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE : D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			renderInstance.material.diffuseTexIndex = getTextureIndex(instance->getDiffuseTexture());
			renderInstance.material.normalTexIndex = getTextureIndex(instance->getNormalTexture());
			renderInstance.material.specularTexIndex = getTextureIndex(instance->getSpecularTexture());

			if (instance->hasScissorRect()) {
				RT64_RECT rect = instance->getScissorRect();
				renderInstance.scissorRect.left = rect.x;
				renderInstance.scissorRect.top = screenHeight - rect.y - rect.h;
				renderInstance.scissorRect.right = rect.x + rect.w;
				renderInstance.scissorRect.bottom = screenHeight - rect.y;
			}
			else {
				renderInstance.scissorRect = CD3DX12_RECT(0, 0, 0, 0);
			}

			if (instance->hasViewportRect()) {
				RT64_RECT rect = instance->getViewportRect();
				renderInstance.viewport = CD3DX12_VIEWPORT(
					static_cast<float>(rect.x),
					static_cast<float>(screenHeight - rect.y - rect.h),
					static_cast<float>(rect.w),
					static_cast<float>(rect.h)
				);
			}
			else {
				renderInstance.viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 0.0f, 0.0f);
			}

			if (renderInstance.bottomLevelAS != nullptr) {
				rtInstances.push_back(renderInstance);
			}
			else if (instFlags & RT64_INSTANCE_RASTER_BACKGROUND) {
				rasterBgInstances.push_back(renderInstance);
			}
			else {
				rasterFgInstances.push_back(renderInstance);
			}
		}

		// Create the acceleration structures used by the raytracer.
		if (!rtInstances.empty()) {
			createTopLevelAS(rtInstances);
		}

		// Create the instance buffers for the active instances (if necessary).
		createInstanceTransformsBuffer();
		createInstanceMaterialsBuffer();
		
		// Create the buffer containing the raytracing result (always output in a
		// UAV), and create the heap referencing the resources used by the raytracing,
		// such as the acceleration structure
		createShaderResourceHeap();
		
		// Create the shader binding table and indicating which shaders
		// are invoked for each instance in the AS.
		createShaderBindingTable();

		// Update the instance buffers for the active instances.
		updateInstanceTransformsBuffer();
		updateInstanceMaterialsBuffer();
	}
	else {
		rtInstances.clear();
		rasterBgInstances.clear();
		rasterFgInstances.clear();
	}

	RT64_LOG_PRINTF("Finished view update");
}

void RT64::View::render() {
	RT64_LOG_PRINTF("Started view render");

	if (descriptorHeap == nullptr) {
		return;
	}

	auto viewport = scene->getDevice()->getD3D12Viewport();
	auto scissorRect = scene->getDevice()->getD3D12ScissorRect();
	auto d3dCommandList = scene->getDevice()->getD3D12CommandList();
	auto d3d12RenderTarget = scene->getDevice()->getD3D12RenderTarget();
	std::vector<ID3D12DescriptorHeap *> heaps = { descriptorHeap, samplerHeap };

	// Configure the current viewport.
	auto resetScissor = [this, d3dCommandList, &scissorRect]() {
		d3dCommandList->RSSetScissorRects(1, &scissorRect);
		scissorApplied = false;
	};

	auto resetViewport = [this, d3dCommandList, &viewport]() {
		d3dCommandList->RSSetViewports(1, &viewport);
		viewportApplied = false;
	};

	auto applyScissor = [this, d3dCommandList, resetScissor](const CD3DX12_RECT &rect) {
		if (rect.right > rect.left) {
			d3dCommandList->RSSetScissorRects(1, &rect);
			scissorApplied = true;
		}
		else if (scissorApplied) {
			resetScissor();
		}
	};

	auto applyViewport = [this, d3dCommandList, resetViewport](const CD3DX12_VIEWPORT &viewport) {
		if ((viewport.Width > 0) && (viewport.Height > 0)) {
			d3dCommandList->RSSetViewports(1, &viewport);
			viewportApplied = true;
		}
		else if (viewportApplied) {
			resetViewport();
		}
	};

	auto drawInstances = [d3dCommandList, &scissorRect, &heaps, applyScissor, applyViewport, this](const std::vector<RT64::View::RenderInstance> &rasterInstances, UINT baseInstanceIndex, bool applyScissorsAndViewports) {
		d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		UINT rasterSz = (UINT)(rasterInstances.size());
		Shader *previousShader = nullptr;
		for (UINT j = 0; j < rasterSz; j++) {
			const RenderInstance &renderInstance = rasterInstances[j];
			if (applyScissorsAndViewports) {
				applyScissor(renderInstance.scissorRect);
				applyViewport(renderInstance.viewport);
			}

			if (previousShader != renderInstance.shader) {
				const auto &rasterGroup = renderInstance.shader->getRasterGroup();
				d3dCommandList->SetPipelineState(rasterGroup.pipelineState);
				d3dCommandList->SetGraphicsRootSignature(rasterGroup.rootSignature);
				previousShader = renderInstance.shader;
			}

			if (j == 0) {
				d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
				d3dCommandList->SetGraphicsRootDescriptorTable(1, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
				d3dCommandList->SetGraphicsRootDescriptorTable(2, samplerHeap->GetGPUDescriptorHandleForHeapStart());
			}

			d3dCommandList->SetGraphicsRoot32BitConstant(0, baseInstanceIndex + j, 0);
			d3dCommandList->IASetVertexBuffers(0, 1, renderInstance.vertexBufferView);
			d3dCommandList->IASetIndexBuffer(renderInstance.indexBufferView);
			d3dCommandList->DrawIndexedInstanced(renderInstance.indexCount, 1, 0, 0, 0);
		}
	};

	RT64_LOG_PRINTF("Updating global parameters");

	// Determine whether to use the viewport and scissor from the first RT Instance or not.
	// TODO: Some less hackish way to determine what viewport to use for the raytraced content perhaps.
	CD3DX12_RECT rtScissorRect = scissorRect;
	CD3DX12_VIEWPORT rtViewport = viewport;
	if (!rtInstances.empty()) {
		rtScissorRect = rtInstances[0].scissorRect;
		rtViewport = rtInstances[0].viewport;
		if ((rtScissorRect.right <= rtScissorRect.left)) {
			rtScissorRect = scissorRect;
		}

		if ((rtViewport.Width == 0) || (rtViewport.Height == 0)) {
			rtViewport = viewport;
		}

#	ifdef RT64_DLSS
		// Only use jitter when DLSS is active.
		bool jitterActive = rtUpscaleActive && (rtUpscaleMode == UpscaleMode::DLSS);
#	else
		bool jitterActive = false;
#	endif
		if (jitterActive) {
			const int PhaseCount = 64;
			globalParamsBufferData.pixelJitter = HaltonJitter(globalParamsBufferData.frameCount, PhaseCount);
		}
		else {
			globalParamsBufferData.pixelJitter = { 0.0f, 0.0f };
		}

		globalParamsBufferData.viewport.x = rtViewport.TopLeftX;
		globalParamsBufferData.viewport.y = rtViewport.TopLeftY;
		globalParamsBufferData.viewport.z = rtViewport.Width;
		globalParamsBufferData.viewport.w = rtViewport.Height;

		updateGlobalParamsBuffer();
		updateFilterParamsBuffer();

		// Update FSR buffers.
		if (rtUpscaleActive && (rtUpscaleMode == UpscaleMode::FSR)) {
			updateUpscalingParamsBuffer();
			updateSharpenParamsBuffer();
		}
	}

	// Draw the background instances to the screen.
	RT64_LOG_PRINTF("Drawing background instances");
	resetScissor();
	resetViewport();
	drawInstances(rasterBgInstances, (UINT)(rtInstances.size()), true);

	// Draw the background instances to a buffer that can be used by the tracer as an environment map.
	{
		// Transition the background texture render target.
		CD3DX12_RESOURCE_BARRIER bgBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rasterBg.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		d3dCommandList->ResourceBarrier(1, &bgBarrier);
		
		// Set as render target and clear it.
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rasterBgHeap->GetCPUDescriptorHandleForHeapStart(), 0, outputRtvDescriptorSize);
		const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		d3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		d3dCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		
		// Draw background instances to it.
		RT64_LOG_PRINTF("Drawing background instances to render target");
		resetScissor();
		resetViewport();
		drawInstances(rasterBgInstances, (UINT)(rtInstances.size()), false);
		
		// Transition the the background from render target to SRV.
		bgBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rasterBg.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &bgBarrier);
	}

	// Raytracing.
	if (!rtInstances.empty()) {
		RT64_LOG_PRINTF("Drawing raytraced instances");

		// Ray generation.
		D3D12_DISPATCH_RAYS_DESC desc = {};
		uint32_t rayGenerationSectionSizeInBytes = sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = sbtHelper.GetRayGenEntrySize();

		// Miss shader table.
		uint32_t missSectionSizeInBytes = sbtHelper.GetMissSectionSize();
		desc.MissShaderTable.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
		desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
		desc.MissShaderTable.StrideInBytes = sbtHelper.GetMissEntrySize();

		// Hit group table.
		uint32_t hitGroupsSectionSize = sbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
		desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
		desc.HitGroupTable.StrideInBytes = sbtHelper.GetHitGroupEntrySize();
		
		// Dimensions.
		desc.Width = rtWidth;
		desc.Height = rtHeight;
		desc.Depth = 1;

		// Make sure all these buffers are usable as UAVs.
		CD3DX12_RESOURCE_BARRIER preDispatchBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(rtDiffuse.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(rtReflection.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(rtRefraction.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(rtTransparent.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(rtFlow.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(rtDepth[rtSwap ? 1 : 0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};

		d3dCommandList->ResourceBarrier(_countof(preDispatchBarriers), preDispatchBarriers);

		// Bind pipeline and dispatch primary rays.
		RT64_LOG_PRINTF("Dispatching primary rays");
		d3dCommandList->SetPipelineState1(scene->getDevice()->getD3D12RtStateObject());
		d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
		d3dCommandList->DispatchRays(&desc);

		// Barriers for shading buffers before dispatching secondary rays.
		CD3DX12_RESOURCE_BARRIER shadingBarriers[] = {
				CD3DX12_RESOURCE_BARRIER::UAV(rtViewDirection.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtShadingPosition.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtShadingNormal.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtShadingSpecular.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtReflection.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtRefraction.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtInstanceId.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtNormal[rtSwap ? 1 : 0].Get()),
		};

		d3dCommandList->ResourceBarrier(_countof(shadingBarriers), shadingBarriers);

		// Dispatch rays for direct light.
		RT64_LOG_PRINTF("Dispatching direct light rays");
		desc.RayGenerationShaderRecord.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress() + sbtHelper.GetRayGenEntrySize();
		d3dCommandList->DispatchRays(&desc);

		// Dispatch rays for indirect light.
		RT64_LOG_PRINTF("Dispatching indirect light rays");
		desc.RayGenerationShaderRecord.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress() + sbtHelper.GetRayGenEntrySize() * 2;
		d3dCommandList->DispatchRays(&desc);

		// Wait until indirect light is done before dispatching reflection or refraction rays.
		// TODO: This is only required to prevent simultaneous usage of the anyhit buffers.
		// This barrier can be removed if this no longer happens, resulting in less serialization of the commands.
		CD3DX12_RESOURCE_BARRIER indirectBarrier = CD3DX12_RESOURCE_BARRIER::UAV(rtIndirectLightAccum[rtSwap ? 1 : 0].Get());
		d3dCommandList->ResourceBarrier(1, &indirectBarrier);

		// Dispatch rays for refraction.
		RT64_LOG_PRINTF("Dispatching refraction rays");
		desc.RayGenerationShaderRecord.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress() + sbtHelper.GetRayGenEntrySize() * 4;
		d3dCommandList->DispatchRays(&desc);

		// Wait until refraction is done before dispatching reflection rays.
		// TODO: This is only required to prevent simultaneous usage of the anyhit buffers.
		// This barrier can be removed if this no longer happens, resulting in less serialization of the commands.
		CD3DX12_RESOURCE_BARRIER refractionBarrier = CD3DX12_RESOURCE_BARRIER::UAV(rtRefraction.Get());
		d3dCommandList->ResourceBarrier(1, &refractionBarrier);

		// Reflection passes.
		int reflections = maxReflections;
		while (reflections > 0) {
			// Dispatch rays for reflection.
			RT64_LOG_PRINTF("Dispatching reflection rays");
			desc.RayGenerationShaderRecord.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress() + sbtHelper.GetRayGenEntrySize() * 3;
			d3dCommandList->DispatchRays(&desc);
			reflections--;

			// Add a barrier to wait for the input UAVs to be finished if there's more passes left to be done.
			if (reflections > 0) {
				CD3DX12_RESOURCE_BARRIER newInputBarriers[] = {
					CD3DX12_RESOURCE_BARRIER::UAV(rtViewDirection.Get()),
					CD3DX12_RESOURCE_BARRIER::UAV(rtShadingNormal.Get()),
					CD3DX12_RESOURCE_BARRIER::UAV(rtInstanceId.Get()),
					CD3DX12_RESOURCE_BARRIER::UAV(rtReflection.Get())
				};

				d3dCommandList->ResourceBarrier(_countof(newInputBarriers), newInputBarriers);
			}
		}

		// Copy direct light raw buffer to the first direct filtered buffer.
#	ifdef DI_DENOISING_SUPPORT
		bool denoiseDI = denoiserEnabled && (globalParamsBufferData.diSamples > 0);
#	else
		bool denoiseDI = false;
#	endif
		{
			ID3D12Resource *source = rtDirectLightAccum[rtSwap ? 1 : 0].Get();
			ID3D12Resource *dest = rtFilteredDirectLight[denoiseDI ? 0 : 1].Get();

			CD3DX12_RESOURCE_BARRIER beforeCopyBarriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)
			};

			d3dCommandList->ResourceBarrier(_countof(beforeCopyBarriers), beforeCopyBarriers);

			d3dCommandList->CopyResource(dest, source);

			CD3DX12_RESOURCE_BARRIER afterCopyBarriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			};

			d3dCommandList->ResourceBarrier(_countof(afterCopyBarriers), afterCopyBarriers);
		}

#	ifdef DI_DENOISING_SUPPORT
		// Apply a gaussian filter to the direct light with a compute shader.
		if (denoiseDI) {
			for (int i = 0; i < 3; i++) {
				const int ThreadGroupWorkCount = 8;
				int dispatchX = rtWidth / ThreadGroupWorkCount + ((rtWidth % ThreadGroupWorkCount) ? 1 : 0);
				int dispatchY = rtHeight / ThreadGroupWorkCount + ((rtHeight % ThreadGroupWorkCount) ? 1 : 0);
				d3dCommandList->SetPipelineState(scene->getDevice()->getGaussianFilterRGB3x3PipelineState());
				d3dCommandList->SetComputeRootSignature(scene->getDevice()->getGaussianFilterRGB3x3RootSignature());
				d3dCommandList->SetDescriptorHeaps(1, &directFilterHeaps[i % 2]);
				d3dCommandList->SetComputeRootDescriptorTable(0, directFilterHeaps[i % 2]->GetGPUDescriptorHandleForHeapStart());
				d3dCommandList->Dispatch(dispatchX, dispatchY, 1);

				CD3DX12_RESOURCE_BARRIER afterBlurBarriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(rtFilteredDirectLight[(i % 2) ? 1 : 0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(rtFilteredDirectLight[(i % 2) ? 0 : 1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
				};

				d3dCommandList->ResourceBarrier(_countof(afterBlurBarriers), afterBlurBarriers);
			}
		}
#	endif

		// Copy indirect light raw buffer to the first indirect filtered buffer.
		bool denoiseGI = denoiserEnabled && (globalParamsBufferData.giSamples > 0);
		{
			ID3D12Resource *source = rtIndirectLightAccum[rtSwap ? 1 : 0].Get();
			ID3D12Resource *dest = rtFilteredIndirectLight[denoiseGI ? 0 : 1].Get();

			CD3DX12_RESOURCE_BARRIER beforeCopyBarriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)
			};

			d3dCommandList->ResourceBarrier(_countof(beforeCopyBarriers), beforeCopyBarriers);

			d3dCommandList->CopyResource(dest, source);

			CD3DX12_RESOURCE_BARRIER afterCopyBarriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			};

			d3dCommandList->ResourceBarrier(_countof(afterCopyBarriers), afterCopyBarriers);
		}

		// Apply a gaussian filter to the indirect light with a compute shader.
		if (denoiseGI) {
			for (int i = 0; i < 5; i++) {
				const int ThreadGroupWorkCount = 8;
				int dispatchX = rtWidth / ThreadGroupWorkCount + ((rtWidth % ThreadGroupWorkCount) ? 1 : 0);
				int dispatchY = rtHeight / ThreadGroupWorkCount + ((rtHeight % ThreadGroupWorkCount) ? 1 : 0);
				d3dCommandList->SetPipelineState(scene->getDevice()->getGaussianFilterRGB3x3PipelineState());
				d3dCommandList->SetComputeRootSignature(scene->getDevice()->getGaussianFilterRGB3x3RootSignature());
				d3dCommandList->SetDescriptorHeaps(1, &indirectFilterHeaps[i % 2]);
				d3dCommandList->SetComputeRootDescriptorTable(0, indirectFilterHeaps[i % 2]->GetGPUDescriptorHandleForHeapStart());
				d3dCommandList->Dispatch(dispatchX, dispatchY, 1);

				CD3DX12_RESOURCE_BARRIER afterBlurBarriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(rtFilteredIndirectLight[(i % 2) ? 1 : 0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(rtFilteredIndirectLight[(i % 2) ? 0 : 1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
				};

				d3dCommandList->ResourceBarrier(_countof(afterBlurBarriers), afterBlurBarriers);
			}
		}

		// Compose the output buffer.
		ID3D12Resource *rtOutputCur = rtOutput[rtSwap ? 1 : 0].Get();

		// Barriers for shading buffers after rays are finished.
		CD3DX12_RESOURCE_BARRIER afterDispatchBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(rtOutputCur, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(rtDiffuse.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(rtReflection.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(rtRefraction.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(rtTransparent.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};

		d3dCommandList->ResourceBarrier(_countof(afterDispatchBarriers), afterDispatchBarriers);

		// Set the output as the current render target.
		CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtvHandle(outputBgHeap[rtSwap ? 1 : 0]->GetCPUDescriptorHandleForHeapStart(), 0, outputRtvDescriptorSize);
		d3dCommandList->OMSetRenderTargets(1, &outputRtvHandle, FALSE, nullptr);

		// Apply the scissor and viewport to the size of the output texture.
		applyScissor(CD3DX12_RECT(0, 0, static_cast<LONG>(rtWidth), static_cast<LONG>(rtHeight)));
		applyViewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(rtWidth), static_cast<float>(rtHeight)));

		// Draw the raytracing output.
		RT64_LOG_PRINTF("Composing the raytracing output");
		std::vector<ID3D12DescriptorHeap *> composeHeaps = { composeHeap };
		d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(composeHeaps.size()), composeHeaps.data());
		d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		d3dCommandList->IASetVertexBuffers(0, 0, nullptr);
		d3dCommandList->SetPipelineState(scene->getDevice()->getComposePipelineState());
		d3dCommandList->SetGraphicsRootSignature(scene->getDevice()->getComposeRootSignature());
		d3dCommandList->SetGraphicsRootDescriptorTable(0, composeHeap->GetGPUDescriptorHandleForHeapStart());
		d3dCommandList->DrawInstanced(3, 1, 0, 0);

		// Switch output to a pixel shader resource.
		CD3DX12_RESOURCE_BARRIER afterComposeBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(rtOutputCur, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(rtFilteredDirectLight[1].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(rtFilteredIndirectLight[1].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};

		d3dCommandList->ResourceBarrier(_countof(afterComposeBarriers), afterComposeBarriers);

		// Transition the motion vectors and depth buffer to shader resources.
		CD3DX12_RESOURCE_BARRIER beforeFiltersBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(rtFlow.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(rtDepth[rtSwap ? 1 : 0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};

		d3dCommandList->ResourceBarrier(_countof(beforeFiltersBarriers), beforeFiltersBarriers);

		if (rtUpscaleActive) {
			if (rtUpscaleMode == UpscaleMode::FSR) {
				RT64_LOG_PRINTF("Upscaling frame");

				// Switch output to UAV.
				CD3DX12_RESOURCE_BARRIER beforeEasuBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutputUpscaled.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				d3dCommandList->ResourceBarrier(1, &beforeEasuBarrier);

				// Execute the compute shader for upscaling.
				std::vector<ID3D12DescriptorHeap *> upscaleHeaps = { upscaleHeap };
				static const int threadGroupWorkRegionDim = 16;
				int width = scene->getDevice()->getWidth();
				int height = scene->getDevice()->getHeight();
				int dispatchX = (width + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
				int dispatchY = (height + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
				d3dCommandList->SetPipelineState(scene->getDevice()->getFsrEasuPipelineState());
				d3dCommandList->SetComputeRootSignature(scene->getDevice()->getFsrEasuRootSignature());
				d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(upscaleHeaps.size()), upscaleHeaps.data());
				d3dCommandList->SetComputeRootDescriptorTable(0, upscaleHeap->GetGPUDescriptorHandleForHeapStart());
				d3dCommandList->Dispatch(dispatchX, dispatchY, 1);

				// Switch output to shader resource.
				CD3DX12_RESOURCE_BARRIER afterEasuBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutputUpscaled.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				d3dCommandList->ResourceBarrier(1, &afterEasuBarrier);
			}
#		ifdef RT64_DLSS
			else if (rtUpscaleMode == UpscaleMode::DLSS) {
				// Switch output to UAV.
				CD3DX12_RESOURCE_BARRIER beforeDlssBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutputUpscaled.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				d3dCommandList->ResourceBarrier(1, &beforeDlssBarrier);

				// Execute DLSS.
				DLSS::UpscaleParameters params;
				params.inRect = { 0, 0, rtWidth, rtHeight };
				params.inColor = rtOutputCur;
				params.inFlow = rtFlow.Get();
				params.inDepth = rtDepth[rtSwap ? 1 : 0].Get();
				params.outColor = rtOutputUpscaled.Get();
				params.resetAccumulation = false; // TODO: Make this configurable via the API.
				params.sharpness = dlssSharpness;
				params.jitterX = -globalParamsBufferData.pixelJitter.x;
				params.jitterY = -globalParamsBufferData.pixelJitter.y;
				dlss->upscale(params);

				// Switch output to shader resource.
				CD3DX12_RESOURCE_BARRIER afterDlssBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutputUpscaled.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				d3dCommandList->ResourceBarrier(1, &afterDlssBarrier);
			}
#		endif
			else {
				assert(false && "Unimplemented upscaling mode.");
			}
		}

		if (rtSharpenActive) {
			if (rtUpscaleMode == UpscaleMode::FSR) {
				RT64_LOG_PRINTF("Sharpening frame");

				// Switch output to UAV.
				CD3DX12_RESOURCE_BARRIER beforeRcasBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutputSharpened.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				d3dCommandList->ResourceBarrier(1, &beforeRcasBarrier);

				// Execute the compute shader for sharpening.
				std::vector<ID3D12DescriptorHeap *> sharpenHeaps = { sharpenHeap };
				static const int threadGroupWorkRegionDim = 16;
				int width = rtUpscaleActive ? scene->getDevice()->getWidth() : rtWidth;
				int height = rtUpscaleActive ? scene->getDevice()->getHeight() : rtHeight;
				int dispatchX = (width + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
				int dispatchY = (height + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
				d3dCommandList->SetPipelineState(scene->getDevice()->getFsrRcasPipelineState());
				d3dCommandList->SetComputeRootSignature(scene->getDevice()->getFsrRcasRootSignature());
				d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(sharpenHeaps.size()), sharpenHeaps.data());
				d3dCommandList->SetComputeRootDescriptorTable(0, sharpenHeap->GetGPUDescriptorHandleForHeapStart());
				d3dCommandList->Dispatch(dispatchX, dispatchY, 1);

				// Switch output to shader resource.
				CD3DX12_RESOURCE_BARRIER afterRcasBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutputSharpened.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				d3dCommandList->ResourceBarrier(1, &afterRcasBarrier);
			}
			else {
				assert(false && "Unimplemented sharpen mode.");
			}
		}

		// Set the final render target.
		CD3DX12_CPU_DESCRIPTOR_HANDLE finalRtvHandle = scene->getDevice()->getD3D12RTV();
		d3dCommandList->OMSetRenderTargets(1, &finalRtvHandle, FALSE, nullptr);

		// Apply the same scissor and viewport that was determined for the raytracing step.
		applyScissor(rtScissorRect);
		applyViewport(rtViewport);

		// Draw the output to the screen.
		if (globalParamsBufferData.visualizationMode == VisualizationModeFinal) {
			RT64_LOG_PRINTF("Drawing final output");
			std::vector<ID3D12DescriptorHeap *> postProcessHeaps = { postProcessHeap };
			d3dCommandList->SetPipelineState(scene->getDevice()->getPostProcessPipelineState());
			d3dCommandList->SetGraphicsRootSignature(scene->getDevice()->getPostProcessRootSignature());
			d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(postProcessHeaps.size()), postProcessHeaps.data());
			d3dCommandList->SetGraphicsRootDescriptorTable(0, postProcessHeap->GetGPUDescriptorHandleForHeapStart());
			d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			d3dCommandList->IASetVertexBuffers(0, 0, nullptr);
			d3dCommandList->DrawInstanced(3, 1, 0, 0);
		}
		// Draw the debugging view.
		else {
			RT64_LOG_PRINTF("Drawing debug view");
			d3dCommandList->SetPipelineState(scene->getDevice()->getDebugPipelineState());
			d3dCommandList->SetGraphicsRootSignature(scene->getDevice()->getDebugRootSignature());
			d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
			d3dCommandList->SetGraphicsRootDescriptorTable(0, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
			d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			d3dCommandList->IASetVertexBuffers(0, 0, nullptr);
			d3dCommandList->DrawInstanced(3, 1, 0, 0);
		}
	}
	else {
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = scene->getDevice()->getD3D12RTV();
		d3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
	}
	
	// Draw the foreground to the screen.
	RT64_LOG_PRINTF("Drawing foreground instances");
	resetScissor();
	resetViewport();
	drawInstances(rasterFgInstances, (UINT)(rasterBgInstances.size() + rtInstances.size()), true);

	// End the frame.
	rtSwap = !rtSwap;
	rtSkipReprojection = false;
	rtHitInstanceIdReadbackUpdated = false;
	globalParamsBufferData.frameCount++;

	RT64_LOG_PRINTF("Finished view render");
}

void RT64::View::renderInspector(Inspector *inspector) {
	if (Im3d::GetDrawListCount() > 0) {
		auto d3dCommandList = scene->getDevice()->getD3D12CommandList();
		auto viewport = scene->getDevice()->getD3D12Viewport();
		auto scissorRect = scene->getDevice()->getD3D12ScissorRect();
		d3dCommandList->SetGraphicsRootSignature(scene->getDevice()->getIm3dRootSignature());

		std::vector<ID3D12DescriptorHeap *> heaps = { descriptorHeap };
		d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
		d3dCommandList->SetGraphicsRootDescriptorTable(0, descriptorHeap->GetGPUDescriptorHandleForHeapStart());

		d3dCommandList->RSSetViewports(1, &viewport);
		d3dCommandList->RSSetScissorRects(1, &scissorRect);

		unsigned int totalVertexCount = 0;
		for (Im3d::U32 i = 0, n = Im3d::GetDrawListCount(); i < n; ++i) {
			auto &drawList = Im3d::GetDrawLists()[i];
			totalVertexCount += drawList.m_vertexCount;
		}

		if (totalVertexCount > 0) {
			// Release the previous vertex buffer if it should be bigger.
			if (!im3dVertexBuffer.IsNull() && (totalVertexCount > im3dVertexCount)) {
				im3dVertexBuffer.Release();
			}

			// Create the vertex buffer if it's empty.
			const UINT vertexBufferSize = totalVertexCount * sizeof(Im3d::VertexData);
			if (im3dVertexBuffer.IsNull()) {
				CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
				im3dVertexBuffer = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_UPLOAD, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);
				im3dVertexCount = totalVertexCount;
				im3dVertexBufferView.BufferLocation = im3dVertexBuffer.Get()->GetGPUVirtualAddress();
				im3dVertexBufferView.StrideInBytes = sizeof(Im3d::VertexData);
				im3dVertexBufferView.SizeInBytes = vertexBufferSize;
			}

			// Copy data to vertex buffer.
			UINT8 *pDataBegin;
			CD3DX12_RANGE readRange(0, 0);
			D3D12_CHECK(im3dVertexBuffer.Get()->Map(0, &readRange, reinterpret_cast<void **>(&pDataBegin)));
			for (Im3d::U32 i = 0, n = Im3d::GetDrawListCount(); i < n; ++i) {
				auto &drawList = Im3d::GetDrawLists()[i];
				size_t copySize = sizeof(Im3d::VertexData) * drawList.m_vertexCount;
				memcpy(pDataBegin, drawList.m_vertexData, copySize);
				pDataBegin += copySize;
			}
			im3dVertexBuffer.Get()->Unmap(0, nullptr);

			unsigned int vertexOffset = 0;
			for (Im3d::U32 i = 0, n = Im3d::GetDrawListCount(); i < n; ++i) {
				auto &drawList = Im3d::GetDrawLists()[i];
				d3dCommandList->IASetVertexBuffers(0, 1, &im3dVertexBufferView);
				switch (drawList.m_primType) {
				case Im3d::DrawPrimitive_Points:
					d3dCommandList->SetPipelineState(scene->getDevice()->getIm3dPipelineStatePoint());
					d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
					break;
				case Im3d::DrawPrimitive_Lines:
					d3dCommandList->SetPipelineState(scene->getDevice()->getIm3dPipelineStateLine());
					d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
					break;
				case Im3d::DrawPrimitive_Triangles:
					d3dCommandList->SetPipelineState(scene->getDevice()->getIm3dPipelineStateTriangle());
					d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					break;
				default:
					break;
				}

				d3dCommandList->DrawInstanced(drawList.m_vertexCount, 1, vertexOffset, 0);
				vertexOffset += drawList.m_vertexCount;
			}
		}
	}
}

void RT64::View::setPerspective(RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist) {
	// Ignore all external calls to set the perspective when control override is active.
	if (perspectiveControlActive) {
		return;
	}

	this->fovRadians = fovRadians;
	this->nearDist = nearDist;
	this->farDist = farDist;

	globalParamsBufferData.view = XMMatrixSet(
		viewMatrix.m[0][0], viewMatrix.m[0][1], viewMatrix.m[0][2], viewMatrix.m[0][3],
		viewMatrix.m[1][0], viewMatrix.m[1][1], viewMatrix.m[1][2], viewMatrix.m[1][3],
		viewMatrix.m[2][0], viewMatrix.m[2][1], viewMatrix.m[2][2], viewMatrix.m[2][3],
		viewMatrix.m[3][0], viewMatrix.m[3][1], viewMatrix.m[3][2], viewMatrix.m[3][3]
	);

	globalParamsBufferData.projection = XMMatrixPerspectiveFovRH(fovRadians, scene->getDevice()->getAspectRatio(), nearDist, farDist);
}

void RT64::View::movePerspective(RT64_VECTOR3 localMovement) {
	XMVECTOR offset = XMVector4Transform(XMVectorSet(localMovement.x, localMovement.y, localMovement.z, 0.0f), globalParamsBufferData.viewI);
	XMVECTOR det;
	globalParamsBufferData.view = XMMatrixMultiply(XMMatrixInverse(&det, XMMatrixTranslationFromVector(offset)), globalParamsBufferData.view);
}

void RT64::View::rotatePerspective(float localYaw, float localPitch, float localRoll) {
	XMVECTOR viewPos = XMVector4Transform(XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), globalParamsBufferData.viewI);
	XMVECTOR viewFocus = XMVectorSet(0.0f, 0.0f, -farDist, 1.0f);
	XMVECTOR viewUp = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
	viewFocus = XMVector4Transform(viewFocus, XMMatrixRotationRollPitchYaw(localRoll, localPitch, localYaw));
	viewFocus = XMVector4Transform(viewFocus, globalParamsBufferData.viewI);
	globalParamsBufferData.view = XMMatrixLookAtRH(viewPos, viewFocus, viewUp);
}

void RT64::View::setPerspectiveControlActive(bool v) {
	perspectiveControlActive = v;
}

void RT64::View::setPerspectiveCanReproject(bool v) {
	perspectiveCanReproject = v;
}

RT64_VECTOR3 RT64::View::getViewPosition() {
	XMVECTOR pos = XMVector4Transform(XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), globalParamsBufferData.viewI);
	return { XMVectorGetX(pos), XMVectorGetY(pos), XMVectorGetZ(pos) };
}

RT64_VECTOR3 RT64::View::getViewDirection() {
	XMVECTOR xdir = XMVector4Transform(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), globalParamsBufferData.viewI);
	RT64_VECTOR3 dir = { XMVectorGetX(xdir), XMVectorGetY(xdir), XMVectorGetZ(xdir) };
	float length = Length(dir);
	return dir / length;
}

float RT64::View::getFOVRadians() const {
	return fovRadians;
}

float RT64::View::getNearDistance() const {
	return nearDist;
}

float RT64::View::getFarDistance() const {
	return farDist;
}

void RT64::View::setDISamples(int v) {
	if (globalParamsBufferData.diSamples != v) {
		globalParamsBufferData.diSamples = v;
	}
}

int RT64::View::getDISamples() const {
	return globalParamsBufferData.diSamples;
}

void RT64::View::setGISamples(int v) {
	if (globalParamsBufferData.giSamples != v) {
		globalParamsBufferData.giSamples = v;
	}
}

int RT64::View::getGISamples() const {
	return globalParamsBufferData.giSamples;
}

void RT64::View::setMaxLightSamples(int v) {
	globalParamsBufferData.maxLightSamples = v;
}

int RT64::View::getMaxLightSamples() const {
	return globalParamsBufferData.maxLightSamples;
}

void RT64::View::setMotionBlurStrength(float v) {
	globalParamsBufferData.motionBlurStrength = v;
}

float RT64::View::getMotionBlurStrength() const {
	return globalParamsBufferData.motionBlurStrength;
}

void RT64::View::setMotionBlurSamples(int v) {
	globalParamsBufferData.motionBlurSamples = v;
}

int RT64::View::getMotionBlurSamples() const {
	return globalParamsBufferData.motionBlurSamples;
}

void RT64::View::setVisualizationMode(int v) {
	globalParamsBufferData.visualizationMode = v;
}

int RT64::View::getVisualizationMode() const {
	return globalParamsBufferData.visualizationMode;
}

void RT64::View::setResolutionScale(float v) {
	if (resolutionScale != v) {
		resolutionScale = v;
		rtRecreateBuffers = true;
	}
}

float RT64::View::getResolutionScale() const {
	return resolutionScale;
}

void RT64::View::setMaxReflections(int v) {
	maxReflections = v;
}

int RT64::View::getMaxReflections() const {
	return maxReflections;
}

void RT64::View::setDenoiserEnabled(bool v) {
	denoiserEnabled = v;
}

bool RT64::View::getDenoiserEnabled() const {
	return denoiserEnabled;
}

void RT64::View::setUpscaleMode(UpscaleMode v) {
	if (rtUpscaleMode != v) {
		rtUpscaleMode = v;
		rtRecreateBuffers = true;
	}
}

RT64::UpscaleMode RT64::View::getUpscaleMode() const {
	return rtUpscaleMode;
}

void RT64::View::setSkyPlaneTexture(Texture *texture) {
	skyPlaneTexture = texture;
}

RT64_VECTOR3 RT64::View::getRayDirectionAt(int px, int py) {
	float x = ((px + 0.5f) / getWidth()) * 2.0f - 1.0f;
	float y = ((py + 0.5f) / getHeight()) * 2.0f - 1.0f;
	XMVECTOR target = XMVector4Transform(XMVectorSet(x, -y, 1.0f, 1.0f), globalParamsBufferData.projectionI);
	XMVECTOR rayDirection = XMVector4Transform(XMVectorSetW(target, 0.0f), globalParamsBufferData.viewI);
	rayDirection = XMVector4Normalize(rayDirection);
	return { XMVectorGetX(rayDirection), XMVectorGetY(rayDirection), XMVectorGetZ(rayDirection) };
}

RT64_INSTANCE *RT64::View::getRaytracedInstanceAt(int x, int y) {
	// TODO: This doesn't handle cases properly when nothing was hit at the target pixel and returns
	// the first instance instead. We need to determine what's the best solution for that.
	// TODO: This is broken on deferred renderer at the moment.
	return nullptr;

	/*
	// Copy instance id resource to readback if necessary.
	if (!rtHitInstanceIdReadbackUpdated) {
		auto d3dCommandList = scene->getDevice()->getD3D12CommandList();
		CD3DX12_RESOURCE_BARRIER rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtHitInstanceId.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		d3dCommandList->ResourceBarrier(1, &rtBarrier);
		d3dCommandList->CopyResource(rtHitInstanceIdReadback.Get(), rtHitInstanceId.Get());
		rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtHitInstanceId.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		d3dCommandList->ResourceBarrier(1, &rtBarrier);
		scene->getDevice()->submitCommandList();
		scene->getDevice()->waitForGPU();
		scene->getDevice()->resetCommandList();
		rtHitInstanceIdReadbackUpdated = true;
	}

	// Check resource's bounds.
	x = (int)(x * rtScale);
	y = (int)(y * rtScale);
	if ((x < 0) || (x >= rtWidth) || (y < 0) || (y >= rtHeight)) {
		return nullptr;
	}
	
	// Map the resource read the pixel.
	size_t index = (rtWidth * y + x) * 2;
	uint16_t instanceId = 0;
	uint8_t *pData;
	D3D12_CHECK(rtHitInstanceIdReadback.Get()->Map(0, nullptr, (void **)(&pData)));
	memcpy(&instanceId, pData + index, sizeof(instanceId));
	rtHitInstanceIdReadback.Get()->Unmap(0, nullptr);
	
	// Check the matching instance.
	if (instanceId >= rtInstances.size()) {
		return nullptr;
	}
	
	return (RT64_INSTANCE *)(rtInstances[instanceId].instance);
	*/
}

void RT64::View::resize() {
	rtRecreateBuffers = true;
}

int RT64::View::getWidth() const {
	return scene->getDevice()->getWidth();
}

int RT64::View::getHeight() const {
	return scene->getDevice()->getHeight();
}

#ifdef RT64_DLSS
void RT64::View::setDlssQualityMode(RT64::DLSS::QualityMode v) {
	if (dlssQuality != v) {
		dlssQuality = v;
		rtRecreateBuffers = true;
	}
}

RT64::DLSS::QualityMode RT64::View::getDlssQualityMode() {
	return dlssQuality;
}

void RT64::View::setDlssSharpness(float v) {
	dlssSharpness = v;
}

float RT64::View::getDlssSharpness() const {
	return dlssSharpness;
}

void RT64::View::setDlssResolutionOverride(bool v) {
	if (dlssResolutionOverride != v) {
		dlssResolutionOverride = v;
		rtRecreateBuffers = true;
	}
}

bool RT64::View::getDlssResolutionOverride() const {
	return dlssResolutionOverride;
}

void RT64::View::setDlssAutoExposure(bool v) {
	if (dlssAutoExposure != v) {
		dlssAutoExposure = v;
		rtRecreateBuffers = true;
	}
}

bool RT64::View::getDlssAutoExposure() const {
	return dlssAutoExposure;
}

bool RT64::View::getDlssInitialized() const {
	return dlss->isInitialized();
}
#endif

// Public

DLLEXPORT RT64_VIEW *RT64_CreateView(RT64_SCENE *scenePtr) {
	assert(scenePtr != nullptr);
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	return (RT64_VIEW *)(new RT64::View(scene));
}

DLLEXPORT void RT64_SetViewPerspective(RT64_VIEW* viewPtr, RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist, bool canReproject) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	view->setPerspective(viewMatrix, fovRadians, nearDist, farDist);
	view->setPerspectiveCanReproject(canReproject);
}

DLLEXPORT void RT64_SetViewDescription(RT64_VIEW *viewPtr, RT64_VIEW_DESC viewDesc) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	view->setResolutionScale(viewDesc.resolutionScale);
	view->setMotionBlurStrength(viewDesc.motionBlurStrength);
	view->setMaxLightSamples(viewDesc.maxLightSamples);
	view->setDISamples(viewDesc.softLightSamples);
	view->setGISamples(viewDesc.giBounces);
	view->setDenoiserEnabled(viewDesc.denoiserEnabled);
#ifdef RT64_DLSS
	view->setUpscaleMode((viewDesc.dlssMode != RT64_DLSS_MODE_OFF) ? RT64::UpscaleMode::DLSS : RT64::UpscaleMode::Bilinear);
	switch (viewDesc.dlssMode) {
	case RT64_DLSS_MODE_AUTO:
		view->setDlssQualityMode(RT64::DLSS::QualityMode::Auto);
		break;
	case RT64_DLSS_MODE_MAX_QUALITY:
		view->setDlssQualityMode(RT64::DLSS::QualityMode::MaxQuality);
		break;
	case RT64_DLSS_MODE_BALANCED:
		view->setDlssQualityMode(RT64::DLSS::QualityMode::Balanced);
		break;
	case RT64_DLSS_MODE_MAX_PERFORMANCE:
		view->setDlssQualityMode(RT64::DLSS::QualityMode::MaxPerformance);
		break;
	case RT64_DLSS_MODE_ULTRA_PERFORMANCE:
		view->setDlssQualityMode(RT64::DLSS::QualityMode::UltraPerformance);
		break;
	}
#endif
}

DLLEXPORT void RT64_SetViewSkyPlane(RT64_VIEW *viewPtr, RT64_TEXTURE *texturePtr) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	RT64::Texture *texture = (RT64::Texture *)(texturePtr);
	view->setSkyPlaneTexture(texture);
}

DLLEXPORT RT64_INSTANCE *RT64_GetViewRaytracedInstanceAt(RT64_VIEW *viewPtr, int x, int y) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	return view->getRaytracedInstanceAt(x, y);
}

DLLEXPORT bool RT64_GetViewFeatureSupport(RT64_VIEW *viewPtr, int feature) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	switch (feature) {
#ifdef RT64_DLSS
	case RT64_FEATURE_DLSS:
		return view->getDlssInitialized();
#endif
	case 0:
	default:
		return false;
	}
}

DLLEXPORT void RT64_DestroyView(RT64_VIEW *viewPtr) {
	delete (RT64::View *)(viewPtr);
}

#endif
