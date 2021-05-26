//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Mesh {
	private:
		Device *device;
		AllocatedResource vertexBuffer;
		AllocatedResource vertexBufferUpload;
		D3D12_VERTEX_BUFFER_VIEW d3dVertexBufferView;
		AllocatedResource indexBuffer;
		AllocatedResource indexBufferUpload;
		D3D12_INDEX_BUFFER_VIEW d3dIndexBufferView;
		int vertexCount;
		int vertexStride;
		int indexCount;
		RT64::AccelerationStructureBuffers d3dBottomLevelASBuffers;
		int flags;

		void createBottomLevelAS(std::vector<std::pair<ID3D12Resource *, uint32_t>> vVertexBuffers, std::vector<std::pair<ID3D12Resource *, uint32_t>> vIndexBuffers);
	public:
		Mesh(Device *device, int flags);
		virtual ~Mesh();
		void updateVertexBuffer(void *vertexArray, int vertexCount, int vertexStride);
		ID3D12Resource *getVertexBuffer() const;
		const D3D12_VERTEX_BUFFER_VIEW *getVertexBufferView() const;
		int getVertexCount() const;
		void updateIndexBuffer(unsigned int *indexArray, int indexCount);
		ID3D12Resource *getIndexBuffer() const;
		const D3D12_INDEX_BUFFER_VIEW *getIndexBufferView() const;
		int getIndexCount() const;
		void updateBottomLevelAS();
		ID3D12Resource *getBottomLevelASResult() const;
	};
};