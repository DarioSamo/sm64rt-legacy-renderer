//
// RT64
//

#pragma once

#include "rt64_common.h"

#include <map>

#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

namespace RT64 {
	class Scene;

	class View {
	private:
		struct RenderInstance {
			const D3D12_VERTEX_BUFFER_VIEW* vertexBufferView;
			const D3D12_INDEX_BUFFER_VIEW* indexBufferView;
			int indexCount;
			ID3D12Resource* bottomLevelAS;
			DirectX::XMMATRIX transform;
			RT64_MATERIAL material;
		};

		Scene *scene;
		RT64_VECTOR3 eyePosition;
		RT64_VECTOR3 eyeFocus;
		RT64_VECTOR3 eyeUpDirection;
		float fovRadians;
		float nearDist;
		float farDist;
		XMMATRIX previousViewProj;
		AccelerationStructureBuffers topLevelASBuffers;
		nv_helpers_dx12::TopLevelASGenerator topLevelASGenerator;
		AllocatedResource rasterResources[2];
		ID3D12DescriptorHeap *rasterRtvHeaps[2];
		AllocatedResource rtOutputResource;
		AllocatedResource rtHitDistanceResource;
		AllocatedResource rtHitColorResource;
		AllocatedResource rtHitNormalResource;
		AllocatedResource rtHitInstanceIdResource;
		UINT outputRtvDescriptorSize;
		ID3D12DescriptorHeap *descriptorHeap;
		UINT descriptorHeapEntryCount;
		nv_helpers_dx12::ShaderBindingTableGenerator sbtHelper;
		AllocatedResource sbtStorage;
		UINT64 sbtStorageSize;
		AllocatedResource cameraBuffer;
		uint32_t cameraBufferSize;
		AllocatedResource activeInstancesBufferProps;
		uint32_t activeInstancesBufferPropsSize;
		std::vector<RenderInstance> rasterBgInstances;
		std::vector<RenderInstance> rasterFgInstances;
		std::vector<RenderInstance> rtInstances;
		std::vector<Texture*> usedTextures;
		
		void createOutputBuffers();
		void releaseOutputBuffers();
		void createInstancePropertiesBuffer();
		void updateInstancePropertiesBuffer();
		void createTopLevelAS(const std::vector<RenderInstance> &rtInstances);
		void createShaderResourceHeap();
		void createShaderBindingTable();
		void createCameraBuffer();
		void updateCameraBuffer();
	public:
		View(Scene *scene);
		virtual ~View();
		void update();
		void render();
		void setPerspectiveLookAt(RT64_VECTOR3 eyePosition, RT64_VECTOR3 eyeFocus, RT64_VECTOR3 eyeUpDirection, float fovRadians, float nearDist, float farDist);
		void resize();
	};
};