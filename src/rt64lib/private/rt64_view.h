//
// RT64
//

#pragma once

#include "rt64_common.h"

#include <map>

#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

namespace RT64 {
	class Denoiser;
	class Scene;
	class Inspector;
	class Instance;
	class Texture;

	class View {
	private:
		struct RenderInstance {
			Instance *instance;
			const D3D12_VERTEX_BUFFER_VIEW* vertexBufferView;
			const D3D12_INDEX_BUFFER_VIEW* indexBufferView;
			int indexCount;
			ID3D12Resource* bottomLevelAS;
			DirectX::XMMATRIX transform;
			RT64_MATERIAL material;
			CD3DX12_RECT scissorRect;
			UINT flags;
		};

		struct ViewParamsBuffer {
			XMMATRIX view;
			XMMATRIX projection;
			XMMATRIX viewI;
			XMMATRIX projectionI;
			XMMATRIX prevViewProj;
			float viewport[4];
			unsigned int randomSeed;
			unsigned int softLightSamples;
			unsigned int giBounces;
			unsigned int maxLightSamples;
			float ambGIMixWeight;
			unsigned int horizontalBlocksCount;
		};

		Scene *scene;
		float fovRadians;
		float nearDist;
		float farDist;
		bool perspectiveControlActive;
		AccelerationStructureBuffers topLevelASBuffers;
		nv_helpers_dx12::TopLevelASGenerator topLevelASGenerator;
		AllocatedResource rasterBg;
		AllocatedResource rasterFg;
		ID3D12DescriptorHeap *rasterBgHeap;
		ID3D12DescriptorHeap *rasterFgHeap;
		AllocatedResource rtOutput;
		AllocatedResource rtAlbedo;
		AllocatedResource rtNormal;
		AllocatedResource rtHitDistance;
		AllocatedResource rtHitColor;
		AllocatedResource rtHitNormal;
		AllocatedResource rtHitInstanceId;
		AllocatedResource rtHitInstanceIdReadback;
		int rtWidth;
		int rtHeight;
		float rtScale;
		float resolutionScale;
		bool denoiserEnabled;
		Denoiser *denoiser;

		bool rtHitInstanceIdReadbackUpdated;
		UINT outputRtvDescriptorSize;
		ID3D12DescriptorHeap *descriptorHeap;
		UINT descriptorHeapEntryCount;
		ID3D12DescriptorHeap *composeHeap;
		nv_helpers_dx12::ShaderBindingTableGenerator sbtHelper;
		AllocatedResource sbtStorage;
		UINT64 sbtStorageSize;
		AllocatedResource viewParamBufferResource;
		ViewParamsBuffer viewParamsBufferData;
		uint32_t viewParamsBufferSize;
		bool viewParamsBufferUpdatedThisFrame;
		AllocatedResource activeInstancesBufferProps;
		uint32_t activeInstancesBufferPropsSize;
		std::vector<RenderInstance> rasterBgInstances;
		std::vector<RenderInstance> rasterFgInstances;
		std::vector<RenderInstance> rtInstances;
		std::vector<Texture*> usedTextures;
		bool scissorApplied;

		AllocatedResource im3dVertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW im3dVertexBufferView;
		unsigned int im3dVertexCount;
		
		void createOutputBuffers();
		void releaseOutputBuffers();
		void createInstancePropertiesBuffer();
		void updateInstancePropertiesBuffer();
		void createTopLevelAS(const std::vector<RenderInstance> &rtInstances);
		void createShaderResourceHeap();
		void createShaderBindingTable();
		void createViewParamsBuffer();
		void updateViewParamsBuffer();
	public:
		View(Scene *scene);
		virtual ~View();
		void update();
		void render();
		void renderInspector(Inspector *inspector);
		void setPerspective(RT64_MATRIX4 viewMatrix, float fovRadians, float nearDist, float farDist);
		void movePerspective(RT64_VECTOR3 localMovement);
		void rotatePerspective(float localYaw, float localPitch, float localRoll);
		void setPerspectiveControlActive(bool v);
		RT64_VECTOR3 getViewPosition();
		RT64_VECTOR3 getViewDirection();
		float getFOVRadians() const;
		float getNearDistance() const;
		float getFarDistance() const;
		void setSoftLightSamples(int v);
		int getSoftLightSamples() const;
		void setGIBounces(int v);
		int getGIBounces() const;
		void setMaxLightSamples(int v);
		int getMaxLightSamples() const;
		void setAmbGIMixWeight(float v);
		float getAmbGIMixWeight() const;
		void setResolutionScale(float v);
		float getResolutionScale() const;
		void setDenoiserEnabled(bool v);
		bool getDenoiserEnabled() const;
		RT64_VECTOR3 getRayDirectionAt(int x, int y);
		RT64_INSTANCE *getRaytracedInstanceAt(int x, int y);
		void resize();
		int getWidth() const;
		int getHeight() const;
	};
};