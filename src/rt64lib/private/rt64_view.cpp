//
// RT64
//

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include <map>
#include <set>

#include "rt64_denoiser.h"
#include "rt64_device.h"
#include "rt64_instance.h"
#include "rt64_mesh.h"
#include "rt64_scene.h"
#include "rt64_shader.h"
#include "rt64_texture.h"
#include "rt64_view.h"

#include "im3d/im3d.h"
#include "xxhash/xxhash32.h"

namespace {
	const int MaxQueries = 16 + 1;
};

// Private

RT64::View::View(Scene *scene) {
	assert(scene != nullptr);
	this->scene = scene;
	descriptorHeap = nullptr;
	descriptorHeapEntryCount = 0;
	composeHeap = nullptr;
	sbtStorageSize = 0;
	activeInstancesBufferTransformsSize = 0;
	activeInstancesBufferMaterialsSize = 0;
	globalParamsBufferData.motionBlurStrength = 0.0f;
	globalParamsBufferData.skyPlaneTexIndex = -1;
	globalParamsBufferData.randomSeed = 0;
	globalParamsBufferData.softLightSamples = 0;
	globalParamsBufferData.giBounces = 0;
	globalParamsBufferData.maxLightSamples = 12;
	globalParamsBufferData.motionBlurSamples = 32;
	globalParamsBufferData.visualizationMode = 0;
	globalParamsBufferData.frameCount = 0;
	globalParamsBufferSize = 0;
	rtSwap = false;
	rtWidth = 0;
	rtHeight = 0;
	rtScale = 1.0f;
	resolutionScale = 1.0f;
	denoiserEnabled = false;
	denoiserTemporal = false;
	denoiser = nullptr;
	perspectiveControlActive = false;
	im3dVertexCount = 0;
	rtHitInstanceIdReadbackUpdated = false;
	skyPlaneTexture = nullptr;
	scissorApplied = false;
	viewportApplied = false;

	createOutputBuffers();
	createGlobalParamsBuffer();

	scene->addView(this);
}

RT64::View::~View() {
	delete denoiser;

	scene->removeView(this);

	releaseOutputBuffers();
}

void RT64::View::createOutputBuffers() {
	releaseOutputBuffers();

	outputRtvDescriptorSize = scene->getDevice()->getD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	int screenWidth = scene->getDevice()->getWidth();
	int screenHeight = scene->getDevice()->getHeight();
	rtWidth = lround(screenWidth * rtScale);
	rtHeight = lround(screenHeight * rtScale);
	globalParamsBufferData.resolution.x = (float)(rtWidth);
	globalParamsBufferData.resolution.y = (float)(rtHeight);
	globalParamsBufferData.resolution.z = (float)(screenWidth);
	globalParamsBufferData.resolution.w = (float)(screenHeight);

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
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	rtOutput[0] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, true, true);
	rtOutput[1] = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, true, true);
	rtAlbedo = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, true, true);
	rtNormal = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, true, true);
	rtFlow = scene->getDevice()->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &resDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, true, true);
	
	// Create hit result buffers.
	UINT64 hitCountBufferSizeOne = rtWidth * rtHeight;
	UINT64 hitCountBufferSizeAll = hitCountBufferSizeOne * MaxQueries;
	rtHitDistAndFlow = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitColor = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitNormal = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 8, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitSpecular = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitInstanceId = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, hitCountBufferSizeAll * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	rtHitInstanceIdReadback = scene->getDevice()->allocateBuffer(D3D12_HEAP_TYPE_READBACK, hitCountBufferSizeOne * 2, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

	// Create the RTVs for the raster resources.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	D3D12_CHECK(scene->getDevice()->getD3D12Device()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rasterBgHeap)));
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvBgHandle(rasterBgHeap->GetCPUDescriptorHandleForHeapStart());
	scene->getDevice()->getD3D12Device()->CreateRenderTargetView(rasterBg.Get(), nullptr, rtvBgHandle);

	if (denoiserEnabled) {
		ID3D12Resource *rtOutputArray[2] = { rtOutput[0].Get(), rtOutput[1].Get() };
		denoiser->set(rtWidth, rtHeight, rtOutputArray, rtAlbedo.Get(), rtNormal.Get(), rtFlow.Get());
	}
}

void RT64::View::releaseOutputBuffers() {
	rasterBg.Release();
	rtOutput[0].Release();
	rtOutput[1].Release();
	rtAlbedo.Release();
	rtNormal.Release();
	rtFlow.Release();
	rtHitDistAndFlow.Release();
	rtHitColor.Release();
	rtHitNormal.Release();
	rtHitSpecular.Release();
	rtHitInstanceId.Release();
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
	assert(usedTextures.size() <= 512);

	uint32_t entryCount = ((uint32_t)(HeapIndices::MAX) - 1) + (uint32_t)(usedTextures.size());

	// Recreate descriptor heap to be bigger if necessary.
	if (descriptorHeapEntryCount < entryCount) {
		if (descriptorHeap != nullptr) {
			descriptorHeap->Release();
			descriptorHeap = nullptr;
		}

		descriptorHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), entryCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		descriptorHeapEntryCount = entryCount;
	}

	const UINT handleIncrement = scene->getDevice()->getD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Get a handle to the heap memory on the CPU side, to be able to write the
	// descriptors directly
	D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// UAV for output buffer.
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtOutput[rtSwap ? 1 : 0].Get(), nullptr, &uavDesc, handle);
	handle.ptr += handleIncrement;

	// UAV for albedo output buffer.
	scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtAlbedo.Get(), nullptr, &uavDesc, handle);
	handle.ptr += handleIncrement;

	// UAV for normal output buffer.
	scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtNormal.Get(), nullptr, &uavDesc, handle);
	handle.ptr += handleIncrement;

	// UAV for motion vector output buffer.
	scene->getDevice()->getD3D12Device()->CreateUnorderedAccessView(rtFlow.Get(), nullptr, &uavDesc, handle);
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

	// Add the texture SRV.
	for (size_t i = 0; i < usedTextures.size(); i++) {
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(usedTextures[i]->getTexture(), &textureSRVDesc, handle);
		usedTextures[i]->setCurrentIndex(-1);
		handle.ptr += handleIncrement;
	}

	{
		// Create the heap for the compose shader.
		if (composeHeap == nullptr) {
			uint32_t handleCount = 3;
			composeHeap = nv_helpers_dx12::CreateDescriptorHeap(scene->getDevice()->getD3D12Device(), handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = composeHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = 1;
		textureSRVDesc.Texture2D.MostDetailedMip = 0;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

		// SRV for denoised texture.
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtOutput[rtSwap ? 1 : 0].Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// SRV for motion vector texture.
		scene->getDevice()->getD3D12Device()->CreateShaderResourceView(rtFlow.Get(), &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// CBV for global parameters.
		scene->getDevice()->getD3D12Device()->CreateConstantBufferView(&cbvDesc, handle);
		handle.ptr += handleIncrement;
	}
}

void RT64::View::createShaderBindingTable() {
	// The SBT helper class collects calls to Add*Program.  If called several
	// times, the helper must be emptied before re-adding shaders.
	sbtHelper.Reset();

	// The pointer to the beginning of the heap is the only parameter required by
	// shaders without root parameters
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	
	// The helper treats both root parameter pointers and heap pointers as void*,
	// while DX12 uses the
	// D3D12_GPU_DESCRIPTOR_HANDLE to define heap pointers. The pointer in this
	// struct is a UINT64, which then has to be reinterpreted as a pointer.
	auto heapPointer = reinterpret_cast<UINT64 *>(srvUavHeapHandle.ptr);

	// The ray generation only uses heap data.
	sbtHelper.AddRayGenerationProgram(scene->getDevice()->getTraceRayGenID(), { heapPointer });
	
	// Miss shaders don't use any external data.
	sbtHelper.AddMissProgram(scene->getDevice()->getSurfaceMissID(), {});
	sbtHelper.AddMissProgram(scene->getDevice()->getShadowMissID(), {});

	// Add the vertex buffers from all the meshes used by the instances to the hit group.
	for (const RenderInstance &rtInstance : rtInstances) {
		const auto &surfaceHitGroup = rtInstance.shader->getSurfaceHitGroup();
		sbtHelper.AddHitGroup(surfaceHitGroup.id, {
			(void *)(rtInstance.vertexBufferView->BufferLocation),
			(void *)(rtInstance.indexBufferView->BufferLocation),
			heapPointer
		});
		
		const auto &shadowHitGroup = rtInstance.shader->getShadowHitGroup();
		sbtHelper.AddHitGroup(shadowHitGroup.id, {
			(void*)(rtInstance.vertexBufferView->BufferLocation),
			(void*)(rtInstance.indexBufferView->BufferLocation),
			heapPointer
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
	if (globalParamsBufferData.giBounces > 0) {
		globalParamsBufferData.ambientLightColor = ToVector4(desc.ambientBaseColor, 0.0f);
	}
	else {
		globalParamsBufferData.ambientLightColor = ToVector4(desc.ambientBaseColor + desc.ambientNoGIColor, 0.0f);
	}

	globalParamsBufferData.eyeLightDiffuseColor = ToVector4(desc.eyeLightDiffuseColor, 0.0f);
	globalParamsBufferData.eyeLightSpecularColor = ToVector4(desc.eyeLightSpecularColor, 0.0f);
	globalParamsBufferData.skyHSLModifier = ToVector4(desc.skyHSLModifier, 0.0f);
	globalParamsBufferData.giDiffuseStrength = desc.giDiffuseStrength;
	globalParamsBufferData.giSkyStrength = desc.giSkyStrength;

	// Previous view and projection matrices.
	globalParamsBufferData.prevViewProj = globalParamsBufferData.viewProj;
	globalParamsBufferData.viewProj = XMMatrixMultiply(globalParamsBufferData.view, globalParamsBufferData.projection);

#ifdef USE_VIEWPROJ_AS_HASH
	// Compute the hash of the view and projection matrices and use it as the random seed.
	// This is to prevent the denoiser from showing movement when the game is paused.
	XXHash32 viewProjHash(0);
	viewProjHash.add(&globalParamsBufferData.view, sizeof(XMMATRIX));
	viewProjHash.add(&globalParamsBufferData.projection, sizeof(XMMATRIX));
	globalParamsBufferData.randomSeed = viewProjHash.hash();
#else
	globalParamsBufferData.randomSeed = globalParamsBufferData.frameCount;
#endif

	// Inverse matrices required for raytracing.
	XMVECTOR det;
	globalParamsBufferData.prevViewI = globalParamsBufferData.viewI;
	globalParamsBufferData.viewI = XMMatrixInverse(&det, globalParamsBufferData.view);
	globalParamsBufferData.projectionI = XMMatrixInverse(&det, globalParamsBufferData.projection);
	
	// Copy the camera buffer data to the resource.
	uint8_t *pData;
	D3D12_CHECK(globalParamBufferResource.Get()->Map(0, nullptr, (void **)&pData));
	memcpy(pData, &globalParamsBufferData, sizeof(GlobalParamsBuffer));
	globalParamBufferResource.Get()->Unmap(0, nullptr);
}

void RT64::View::update() {
	if (rtScale != resolutionScale) {
		rtScale = std::max(std::min(resolutionScale, 2.0f), 0.01f);
		resolutionScale = rtScale;
		createOutputBuffers();
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
	usedTextures.reserve(512);
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
}

void RT64::View::render() {
	if (descriptorHeap == nullptr) {
		return;
	}

	auto viewport = scene->getDevice()->getD3D12Viewport();
	auto scissorRect = scene->getDevice()->getD3D12ScissorRect();
	auto d3dCommandList = scene->getDevice()->getD3D12CommandList();
	auto d3d12RenderTarget = scene->getDevice()->getD3D12RenderTarget();
	std::vector<ID3D12DescriptorHeap *> heaps = { descriptorHeap };

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
			}

			d3dCommandList->SetGraphicsRoot32BitConstant(0, baseInstanceIndex + j, 0);
			d3dCommandList->IASetVertexBuffers(0, 1, renderInstance.vertexBufferView);
			d3dCommandList->IASetIndexBuffer(renderInstance.indexBufferView);
			d3dCommandList->DrawIndexedInstanced(renderInstance.indexCount, 1, 0, 0, 0);
		}
	};

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

		globalParamsBufferData.viewport.x = rtViewport.TopLeftX;
		globalParamsBufferData.viewport.y = rtViewport.TopLeftY;
		globalParamsBufferData.viewport.z = rtViewport.Width;
		globalParamsBufferData.viewport.w = rtViewport.Height;
		updateGlobalParamsBuffer();
	}

	// Draw the background instances to the screen.
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
		resetScissor();
		resetViewport();
		drawInstances(rasterBgInstances, (UINT)(rtInstances.size()), false);
		
		// Transition the the background from render target to SRV.
		bgBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rasterBg.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &bgBarrier);
	}

	// Raytracing.
	if (!rtInstances.empty()) {
		CD3DX12_RESOURCE_BARRIER rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutput[rtSwap ? 1 : 0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		d3dCommandList->ResourceBarrier(1, &rtBarrier);

		CD3DX12_RESOURCE_BARRIER flowBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtFlow.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		d3dCommandList->ResourceBarrier(1, &flowBarrier);

		// Ray generation.
		D3D12_DISPATCH_RAYS_DESC desc = {};
		uint32_t rayGenerationSectionSizeInBytes = sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = sbtStorage.Get()->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

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

		// Bind pipeline and dispatch rays.
		d3dCommandList->SetPipelineState1(scene->getDevice()->getD3D12RtStateObject());
		d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
		d3dCommandList->DispatchRays(&desc);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(rtOutput[rtSwap ? 1 : 0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &barrier);

		// Denoiser.
		if (denoiserEnabled && (denoiser != nullptr)) {
			CD3DX12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::UAV(rtAlbedo.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtNormal.Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(rtFlow.Get())
			};

			d3dCommandList->ResourceBarrier(_countof(barriers), barriers);
			
			// Wait for the raytracing step to be finished.
			// TODO: Maybe use a fence for this instead so we don't need to wait on all of the GPU operations.
			scene->getDevice()->submitCommandList();
			scene->getDevice()->waitForGPU();
			scene->getDevice()->resetCommandList();

			// Execute the denoiser.
			denoiser->denoise(rtSwap);
			rtSwap = !rtSwap;
			
			// Reset the scissor and the viewport since the command list was reset.
			resetScissor();
			resetViewport();
		}
		
		// Apply the same scissor and viewport that was determined for the raytracing step.
		applyScissor(rtScissorRect);
		applyViewport(rtViewport);

		// Transition the motion vector to a shader resource.
		flowBarrier = CD3DX12_RESOURCE_BARRIER::Transition(rtFlow.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &flowBarrier);

		// Set the final render target view.
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = scene->getDevice()->getD3D12RTV();
		d3dCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		// Draw the raytracing output.
		std::vector<ID3D12DescriptorHeap *> composeHeaps = { composeHeap };
		d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(composeHeaps.size()), composeHeaps.data());
		d3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		d3dCommandList->IASetVertexBuffers(0, 0, nullptr);
		d3dCommandList->SetPipelineState(scene->getDevice()->getComposePipelineState());
		d3dCommandList->SetGraphicsRootSignature(scene->getDevice()->getComposeRootSignature());
		d3dCommandList->SetGraphicsRootDescriptorTable(0, composeHeap->GetGPUDescriptorHandleForHeapStart());
		d3dCommandList->DrawInstanced(3, 1, 0, 0);

		// Draw the motion vector output.
		if (globalParamsBufferData.visualizationMode == VisualizationModeMotionVectors) {
			d3dCommandList->SetPipelineState(scene->getDevice()->getDebugMotionVectorsPipelineState());
			d3dCommandList->SetGraphicsRootSignature(scene->getDevice()->getDebugMotionVectorsRootSignature());
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
	resetScissor();
	resetViewport();
	drawInstances(rasterFgInstances, (UINT)(rasterBgInstances.size() + rtInstances.size()), true);

	// Clear flags.
	rtHitInstanceIdReadbackUpdated = false;
	globalParamsBufferData.frameCount++;
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

void RT64::View::setSoftLightSamples(int v) {
	if (globalParamsBufferData.softLightSamples != v) {
		globalParamsBufferData.softLightSamples = v;
	}
}

int RT64::View::getSoftLightSamples() const {
	return globalParamsBufferData.softLightSamples;
}

void RT64::View::setGIBounces(int v) {
	if (globalParamsBufferData.giBounces != v) {
		globalParamsBufferData.giBounces = v;
	}
}

int RT64::View::getGIBounces() const {
	return globalParamsBufferData.giBounces;
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
	resolutionScale = v;
}

float RT64::View::getResolutionScale() const {
	return resolutionScale;
}

void RT64::View::checkDenoiser() {
	// Create the denoiser if it wasn't created yet.
	if (denoiser == nullptr) {
		denoiser = new RT64::Denoiser(scene->getDevice(), denoiserTemporal);
	}

	// Update the buffer sizes since they might've changed since the last time the denoiser was enabled.
	ID3D12Resource *rtOutputArray[2] = { rtOutput[0].Get(), rtOutput[1].Get() };
	denoiser->set(rtWidth, rtHeight, rtOutputArray, rtAlbedo.Get(), rtNormal.Get(), rtFlow.Get());
}

void RT64::View::setDenoiserEnabled(bool v) {
	if (!denoiserEnabled && v) {
		checkDenoiser();
	}

	denoiserEnabled = v;
}

bool RT64::View::getDenoiserEnabled() const {
	return denoiserEnabled;
}

void RT64::View::setDenoiserTemporalMode(bool v) {
	if (denoiserTemporal != v) {
		denoiserTemporal = v;

		// Recreate the denoiser.
		if (denoiser != nullptr) {
			delete denoiser;
			denoiser = nullptr;
			checkDenoiser();
		}
	}
}

bool RT64::View::getDenoiserTemporalMode() const {
	return denoiserTemporal;
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
}

void RT64::View::resize() {
	createOutputBuffers();
}

int RT64::View::getWidth() const {
	return scene->getDevice()->getWidth();
}

int RT64::View::getHeight() const {
	return scene->getDevice()->getHeight();
}

// Public

DLLEXPORT RT64_VIEW *RT64_CreateView(RT64_SCENE *scenePtr) {
	assert(scenePtr != nullptr);
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	return (RT64_VIEW *)(new RT64::View(scene));
}

DLLEXPORT void RT64_SetViewPerspective(RT64_VIEW* viewPtr, RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	view->setPerspective(viewMatrix, fovRadians, nearDist, farDist);
}

DLLEXPORT void RT64_SetViewDescription(RT64_VIEW *viewPtr, RT64_VIEW_DESC viewDesc) {
	assert(viewPtr != nullptr);
	RT64::View *view = (RT64::View *)(viewPtr);
	view->setResolutionScale(viewDesc.resolutionScale);
	view->setMotionBlurStrength(viewDesc.motionBlurStrength);
	view->setMaxLightSamples(viewDesc.maxLightSamples);
	view->setSoftLightSamples(viewDesc.softLightSamples);
	view->setGIBounces(viewDesc.giBounces);
	view->setDenoiserEnabled(viewDesc.denoiserEnabled);
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

DLLEXPORT void RT64_DestroyView(RT64_VIEW *viewPtr) {
	delete (RT64::View *)(viewPtr);
}

#endif
