//
// RT64
//

#ifndef RT64_MINIMAL

#include "../public/rt64.h"
#include "rt64_mesh.h"
#include "rt64_device.h"

// Private

RT64::Mesh::Mesh(Device *device, int flags) {
	assert(device != nullptr);
	this->device = device;
	this->flags = flags;
	vertexCount = 0;
	indexCount = 0;
	vertexStride = 0;
}

RT64::Mesh::~Mesh() {
	vertexBuffer.Release();
	vertexBufferUpload.Release();
	indexBuffer.Release();
	indexBufferUpload.Release();
	d3dBottomLevelASBuffers.Release();
}

void RT64::Mesh::updateVertexBuffer(void *vertexArray, int vertexCount, int vertexStride) {
	const UINT vertexBufferSize = vertexCount * vertexStride;

	if (!vertexBuffer.IsNull() && ((this->vertexCount != vertexCount) || (this->vertexStride != vertexStride))) {
		vertexBuffer.Release();
		vertexBufferUpload.Release();

		// Discard the BLAS since it won't be compatible anymore even if it's updatable.
		d3dBottomLevelASBuffers.Release();
	}

	if (vertexBuffer.IsNull()) {
		CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		vertexBufferUpload = device->allocateResource(D3D12_HEAP_TYPE_UPLOAD, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);

		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		vertexBuffer = device->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
	}

	// Copy data to upload heap.
	UINT8 *pDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	D3D12_CHECK(vertexBufferUpload.Get()->Map(0, &readRange, reinterpret_cast<void**>(&pDataBegin)));
	memcpy(pDataBegin, vertexArray, vertexBufferSize);
	vertexBufferUpload.Get()->Unmap(0, nullptr);
	
	// Copy resource to the real default resource.
	device->getD3D12CommandList()->CopyResource(vertexBuffer.Get(), vertexBufferUpload.Get());

	// Wait for the resource to finish copying before switching to generic read.
	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	device->getD3D12CommandList()->ResourceBarrier(1, &transition);

	// Configure vertex buffer view.
	d3dVertexBufferView.BufferLocation = vertexBuffer.Get()->GetGPUVirtualAddress();
	d3dVertexBufferView.StrideInBytes = vertexStride;
	d3dVertexBufferView.SizeInBytes = vertexBufferSize;

	// Store the new vertex count and stride.
	this->vertexCount = vertexCount;
	this->vertexStride = vertexStride;
}

void RT64::Mesh::updateIndexBuffer(unsigned int *indexArray, int indexCount) {
	const UINT indexBufferSize = indexCount * sizeof(unsigned int);

	if (!indexBuffer.IsNull() && (this->indexCount != indexCount)) {
		indexBuffer.Release();
		indexBufferUpload.Release();

		// Discard the BLAS since it won't be compatible anymore even if it's updatable.
		d3dBottomLevelASBuffers.Release();
	}

	if (indexBuffer.IsNull()) {
		CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
		indexBufferUpload = device->allocateResource(D3D12_HEAP_TYPE_UPLOAD, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);

		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
		indexBuffer = device->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
	}

	// Copy data to upload heap.
	UINT8 *pDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	D3D12_CHECK(indexBufferUpload.Get()->Map(0, &readRange, reinterpret_cast<void **>(&pDataBegin)));
	memcpy(pDataBegin, indexArray, indexBufferSize);
	indexBufferUpload.Get()->Unmap(0, nullptr);
	
	// Copy resource to the real default resource.
	device->getD3D12CommandList()->CopyResource(indexBuffer.Get(), indexBufferUpload.Get());

	// Wait for the resource to finish copying before switching to generic read.
	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	device->getD3D12CommandList()->ResourceBarrier(1, &transition);

	// Configure index buffer view.
	d3dIndexBufferView.BufferLocation = indexBuffer.Get()->GetGPUVirtualAddress();
	d3dIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	d3dIndexBufferView.SizeInBytes = indexBufferSize;

	this->indexCount = indexCount;
}

void RT64::Mesh::updateBottomLevelAS() {
	if (flags & RT64_MESH_RAYTRACE_ENABLED) {
		// Create and store the bottom level AS buffers.
		createBottomLevelAS({ { getVertexBuffer(), getVertexCount() } }, { { getIndexBuffer(), getIndexCount() } });

		// Submit this result as the last barrier for the command queue.
		D3D12_RESOURCE_BARRIER barrier;
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = d3dBottomLevelASBuffers.result.Get();
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		device->setLastCommandQueueBarrier(barrier);
	}
}

void RT64::Mesh::createBottomLevelAS(std::vector<std::pair<ID3D12Resource *, uint32_t>> vVertexBuffers, std::vector<std::pair<ID3D12Resource *, uint32_t>> vIndexBuffers) {
	bool updatable = flags & RT64_MESH_RAYTRACE_UPDATABLE;
	bool fastTrace = flags & RT64_MESH_RAYTRACE_FAST_TRACE;
	bool compact = flags & RT64_MESH_RAYTRACE_COMPACT;
	if (!updatable) {
		// Release the previously stored AS buffers if there's any.
		d3dBottomLevelASBuffers.Release();
	}
	
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;
	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		if ((i < vIndexBuffers.size()) && (vIndexBuffers[i].second > 0)) {
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first, 0, vVertexBuffers[i].second, vertexStride, vIndexBuffers[i].first, 0, vIndexBuffers[i].second, nullptr, 0, true);
		}
		else {
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first, 0, vVertexBuffers[i].second, vertexStride, 0, 0);
		}
	}

	UINT64 resultSizeInBytes = 0;
	UINT64 scratchSizeInBytes = 0;
	ID3D12Resource *previousResult = d3dBottomLevelASBuffers.result.Get();
	bottomLevelAS.ComputeASBufferSizes(device->getD3D12Device(), updatable, compact, fastTrace, &scratchSizeInBytes, &resultSizeInBytes);

	if (d3dBottomLevelASBuffers.result.IsNull()) {
		d3dBottomLevelASBuffers.scratch = device->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
		d3dBottomLevelASBuffers.result = device->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	}

	bottomLevelAS.Generate(device->getD3D12CommandList(), d3dBottomLevelASBuffers.scratch.Get(), d3dBottomLevelASBuffers.result.Get(), (previousResult != nullptr), previousResult);
}

ID3D12Resource *RT64::Mesh::getVertexBuffer() const {
	return vertexBuffer.Get();
}

const D3D12_VERTEX_BUFFER_VIEW *RT64::Mesh::getVertexBufferView() const {
	return &d3dVertexBufferView;
}

int RT64::Mesh::getVertexCount() const {
	return vertexCount;
}

ID3D12Resource *RT64::Mesh::getIndexBuffer() const {
	return indexBuffer.Get();
}

const D3D12_INDEX_BUFFER_VIEW *RT64::Mesh::getIndexBufferView() const {
	return &d3dIndexBufferView;
}

int RT64::Mesh::getIndexCount() const {
	return indexCount;
}

ID3D12Resource *RT64::Mesh::getBottomLevelASResult() const {
	return d3dBottomLevelASBuffers.result.Get();
}

// Public

DLLEXPORT RT64_MESH *RT64_CreateMesh(RT64_DEVICE *devicePtr, int flags) {
	RT64::Device *device = (RT64::Device *)(devicePtr);
	return (RT64_MESH *)(new RT64::Mesh(device, flags));
}

DLLEXPORT void RT64_SetMesh(RT64_MESH *meshPtr, void *vertexArray, int vertexCount, int vertexStride, unsigned int *indexArray, int indexCount) {
	assert(meshPtr != nullptr);
	assert(vertexArray != nullptr);
	assert(vertexCount > 0);
	assert(indexArray != nullptr);
	assert(indexCount > 0);
	RT64::Mesh *mesh = (RT64::Mesh *)(meshPtr);
	mesh->updateVertexBuffer(vertexArray, vertexCount, vertexStride);
	mesh->updateIndexBuffer(indexArray, indexCount);
	mesh->updateBottomLevelAS();
}

DLLEXPORT void RT64_DestroyMesh(RT64_MESH * meshPtr) {
	delete (RT64::Mesh *)(meshPtr);
}

#endif