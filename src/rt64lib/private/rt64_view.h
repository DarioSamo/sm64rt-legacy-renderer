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
	class Shader;
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
			DirectX::XMMATRIX transformPrevious;
			RT64_MATERIAL material;
			Shader *shader;
			CD3DX12_RECT scissorRect;
			CD3DX12_VIEWPORT viewport;
			UINT flags;
		};

		struct GlobalParamsBuffer {
			XMMATRIX view;
			XMMATRIX viewI;
			XMMATRIX prevViewI;
			XMMATRIX projection;
			XMMATRIX projectionI;
			XMMATRIX viewProj;
			XMMATRIX prevViewProj;
			RT64_VECTOR4 viewport;
			RT64_VECTOR4 resolution;
			RT64_VECTOR4 ambientBaseColor;
			RT64_VECTOR4 ambientNoGIColor;
			RT64_VECTOR4 eyeLightDiffuseColor;
			RT64_VECTOR4 eyeLightSpecularColor;
			RT64_VECTOR4 skyDiffuseMultiplier;
			RT64_VECTOR4 skyHSLModifier;
			float skyYawOffset;
			float giDiffuseStrength;
			float giSkyStrength;
			float motionBlurStrength;
			int skyPlaneTexIndex;
			unsigned int randomSeed;
			unsigned int softLightSamples;
			unsigned int giBounces;
			unsigned int maxLightSamples;
			unsigned int motionBlurSamples;
			unsigned int visualizationMode;
			unsigned int frameCount;
		};

		Scene *scene;
		float fovRadians;
		float nearDist;
		float farDist;
		bool perspectiveControlActive;
		bool perspectiveCanReproject;
		AccelerationStructureBuffers topLevelASBuffers;
		nv_helpers_dx12::TopLevelASGenerator topLevelASGenerator;
		AllocatedResource rasterBg;
		ID3D12DescriptorHeap *rasterBgHeap;
		AllocatedResource rtOutput[2];
		AllocatedResource rtAlbedo;
		AllocatedResource rtNormal;
		AllocatedResource rtFlow;
		AllocatedResource rtHitDistAndFlow;
		AllocatedResource rtHitColor;
		AllocatedResource rtHitNormal;
		AllocatedResource rtHitSpecular;
		AllocatedResource rtHitInstanceId;
		AllocatedResource rtHitInstanceIdReadback;
		bool rtSwap;
		int rtWidth;
		int rtHeight;
		float rtScale;
		float resolutionScale;
		bool denoiserEnabled;
		bool denoiserTemporal;
		Denoiser *denoiser;

		bool rtHitInstanceIdReadbackUpdated;
		UINT outputRtvDescriptorSize;
		ID3D12DescriptorHeap *descriptorHeap;
		UINT descriptorHeapEntryCount;
		ID3D12DescriptorHeap *composeHeap;
		nv_helpers_dx12::ShaderBindingTableGenerator sbtHelper;
		AllocatedResource sbtStorage;
		UINT64 sbtStorageSize;
		AllocatedResource globalParamBufferResource;
		GlobalParamsBuffer globalParamsBufferData;
		uint32_t globalParamsBufferSize;
		AllocatedResource activeInstancesBufferTransforms;
		uint32_t activeInstancesBufferTransformsSize;
		AllocatedResource activeInstancesBufferMaterials;
		uint32_t activeInstancesBufferMaterialsSize;
		std::vector<RenderInstance> rasterBgInstances;
		std::vector<RenderInstance> rasterFgInstances;
		std::vector<RenderInstance> rtInstances;
		std::vector<Texture *> usedTextures;
		Texture *skyPlaneTexture;
		bool scissorApplied;
		bool viewportApplied;

		AllocatedResource im3dVertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW im3dVertexBufferView;
		unsigned int im3dVertexCount;
		
		void createOutputBuffers();
		void releaseOutputBuffers();
		void createInstanceTransformsBuffer();
		void updateInstanceTransformsBuffer();
		void createInstanceMaterialsBuffer();
		void updateInstanceMaterialsBuffer();
		void createTopLevelAS(const std::vector<RenderInstance> &rtInstances);
		void createShaderResourceHeap();
		void createShaderBindingTable();
		void createGlobalParamsBuffer();
		void updateGlobalParamsBuffer();
		void checkDenoiser();
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
		void setPerspectiveCanReproject(bool v);
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
		void setMotionBlurStrength(float v);
		float getMotionBlurStrength() const;
		void setMotionBlurSamples(int v);
		int getMotionBlurSamples() const;
		void setVisualizationMode(int v);
		int getVisualizationMode() const;
		void setResolutionScale(float v);
		float getResolutionScale() const;
		void setDenoiserEnabled(bool v);
		bool getDenoiserEnabled() const;
		void setDenoiserTemporalMode(bool v);
		bool getDenoiserTemporalMode() const;
		void setSkyPlaneTexture(Texture *texture);
		RT64_VECTOR3 getRayDirectionAt(int x, int y);
		RT64_INSTANCE *getRaytracedInstanceAt(int x, int y);
		void resize();
		int getWidth() const;
		int getHeight() const;
	};
};