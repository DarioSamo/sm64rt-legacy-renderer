//
// RT64
//

#pragma once

#include "rt64_common.h"

#include <map>

#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

#include "rt64_dlss.h"

namespace RT64 {
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
			RT64_VECTOR4 cameraU;
			RT64_VECTOR4 cameraV;
			RT64_VECTOR4 cameraW;
			RT64_VECTOR4 viewport;
			RT64_VECTOR4 resolution;
			RT64_VECTOR4 ambientBaseColor;
			RT64_VECTOR4 ambientNoGIColor;
			RT64_VECTOR4 eyeLightDiffuseColor;
			RT64_VECTOR4 eyeLightSpecularColor;
			RT64_VECTOR4 skyDiffuseMultiplier;
			RT64_VECTOR4 skyHSLModifier;
			RT64_VECTOR4 ambientFogColor;
			RT64_VECTOR4 groundFogColor;
			RT64_VECTOR2 ambientFogFactors;
			RT64_VECTOR2 groundFogFactors;
			RT64_VECTOR2 groundFogHeightFactors;
			RT64_VECTOR2 pixelJitter;
			float skyYawOffset;
			float giDiffuseStrength;
			float giSkyStrength;
			float motionBlurStrength;
			float tonemapExposure;
			float tonemapWhite;
			float tonemapBlack;
			float tonemapSaturation;
			float tonemapGamma;
			int skyPlaneTexIndex;
			unsigned int randomSeed;
			unsigned int diSamples;
			unsigned int giSamples;
			unsigned int diReproject;
			unsigned int giReproject;
			unsigned int maxLights;
			unsigned int tonemapMode;
			unsigned int motionBlurSamples;
			unsigned int visualizationMode;
			unsigned int frameCount;
			unsigned int volumetricEnabled;
			unsigned int volumetricMaxSamples;
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
		ID3D12DescriptorHeap *outputBgHeap[2];
		AllocatedResource rtOutput[2];
		AllocatedResource rtViewDirection;
		AllocatedResource rtShadingPosition;
		AllocatedResource rtShadingNormal;
		AllocatedResource rtShadingSpecular;
		AllocatedResource rtDiffuse;
		AllocatedResource rtInstanceId;
		AllocatedResource rtFirstInstanceId;
		AllocatedResource rtFirstInstanceIdReadback;
		AllocatedResource rtDirectLightAccum[2];
		AllocatedResource rtFilteredDirectLight[2];
		AllocatedResource rtIndirectLightAccum[2];
		AllocatedResource rtFilteredIndirectLight[2];
		AllocatedResource rtReflection;
		AllocatedResource rtRefraction;
		AllocatedResource rtTransparent;
		AllocatedResource rtVolumetricFog;
		AllocatedResource rtFilteredVolumetricFog;
		AllocatedResource rtFlow;
		AllocatedResource rtNormal[2];
		AllocatedResource rtDepth[2];
		AllocatedResource rtHitDistAndFlow;
		AllocatedResource rtHitColor;
		AllocatedResource rtHitNormal;
		AllocatedResource rtHitSpecular;
		AllocatedResource rtHitInstanceId;
		AllocatedResource rtOutputUpscaled;
		AllocatedResource rtOutputSharpened;
		AllocatedResource rtLumaHistogram;
		AllocatedResource rtLumaAvg;

		float deltaTime;
		bool rtSwap;
		int rtWidth;
		int rtHeight;
		float resolutionScale;
		int maxReflections;
		float sharpenAttenuation;
		bool rtUpscaleActive;
		bool rtSharpenActive;
		UpscaleMode rtUpscaleMode;
		bool rtRecreateBuffers;
		bool rtSkipReprojection;
		bool denoiserEnabled;
		bool volumetricsEnabled;
		UINT rtFirstInstanceIdRowWidth;
		bool rtFirstInstanceIdReadbackUpdated;
		UINT outputRtvDescriptorSize;
		ID3D12DescriptorHeap *descriptorHeap;
		UINT descriptorHeapEntryCount;
		ID3D12DescriptorHeap *samplerHeap;
		ID3D12DescriptorHeap *composeHeap;
		ID3D12DescriptorHeap *upscaleHeap;
		ID3D12DescriptorHeap *sharpenHeap;
		ID3D12DescriptorHeap *lumaHeap;
		ID3D12DescriptorHeap *lumaAvgHeap;
		ID3D12DescriptorHeap *postProcessHeap;
		ID3D12DescriptorHeap *directFilterHeaps[2];
		ID3D12DescriptorHeap *indirectFilterHeaps[2];
		ID3D12DescriptorHeap *volumetricHeap[2];
		ID3D12DescriptorHeap *bicubicHeap;
		nv_helpers_dx12::ShaderBindingTableGenerator sbtHelper;
		AllocatedResource sbtStorage;
		UINT64 sbtStorageSize;
		AllocatedResource globalParamBufferResource;
		GlobalParamsBuffer globalParamsBufferData;
		uint32_t globalParamsBufferSize;
		AllocatedResource upscalingParamBufferResource;
		uint32_t upscalingParamBufferSize;
		AllocatedResource sharpenParamBufferResource;
		uint32_t sharpenParamBufferSize;
		AllocatedResource lumaParamBufferResource;
		uint32_t lumaParamBufferSize;
		AllocatedResource lumaAvgParamBufferResource;
		uint32_t lumaAvgParamBufferSize;
		AllocatedResource filterParamBufferResource;
		uint32_t filterParamBufferSize;
		AllocatedResource volumetricBlurParamBufferResource;
		uint32_t volumetricBlurParamBufferSize;
		AllocatedResource bicubicParamBufferResource;
		uint32_t bicubicParamBufferSize;
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

#ifdef RT64_DLSS
		DLSS *dlss;
		DLSS::QualityMode dlssQuality;
		float dlssSharpness;
		bool dlssAutoExposure;
		bool dlssResolutionOverride;
#endif
		
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
		void createUpscalingParamsBuffer();
		void updateUpscalingParamsBuffer();
		void createSharpenParamsBuffer();
		void updateSharpenParamsBuffer();
		void createLumaParamsBuffer();
		void updateLumaParamsBuffer();
		void createLumaAvgParamsBuffer();
		void updateLumaAvgParamsBuffer();
		void createFilterParamsBuffer();
		void updateFilterParamsBuffer();
		void createVolumetricsBlurParamsBuffer();
		void updateVolumetricsBlurParamsBuffer();
		void createBicubicParamsBuffer();
		void updateBicubicParamsBuffer(int inputRes[2], int outputRes[2]);
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
		void setDISamples(int v);
		int getDISamples() const;
		void setGISamples(int v);
		int getGISamples() const;
		void setMaxLights(int v);
		int getMaxLights() const;
		void setMotionBlurStrength(float v);
		float getMotionBlurStrength() const;
		void setMotionBlurSamples(int v);
		int getMotionBlurSamples() const;
		void setToneMappingMode(int v);
		int getToneMappingMode() const;
		void setTonemapperValues(float e, float w, float b, float s, float g);
		float getToneMapExposure() const;
		float getToneMapWhitePoint() const;
		float getToneMapBlackLevel() const;
		float getToneMapSaturation() const;
		float getToneMapGamma() const;
		void setVolumetricMaxSamples(int v);
		int getVolumetricMaxSamples() const;
		void setVolumetricEnabledFlag(bool v);
		void setVisualizationMode(int v);
		int getVisualizationMode() const;
		bool getVolumetricEnabledFlag() const;
		void setResolutionScale(float v);
		float getResolutionScale() const;
		void setMaxReflections(int v);
		int getMaxReflections() const;
		void setDenoiserEnabled(bool v);
		bool getDenoiserEnabled() const;
		void setUpscaleMode(UpscaleMode v);
		UpscaleMode getUpscaleMode() const;
		void setSkyPlaneTexture(Texture *texture);
		RT64_VECTOR3 getRayDirectionAt(int x, int y);
		RT64_INSTANCE *getRaytracedInstanceAt(int x, int y);
		void resize();
		int getWidth() const;
		int getHeight() const;

#ifdef RT64_DLSS
		void setDlssQualityMode(RT64::DLSS::QualityMode v);
		DLSS::QualityMode getDlssQualityMode();
		void setDlssSharpness(float v);
		float getDlssSharpness() const;
		void setDlssResolutionOverride(bool v);
		bool getDlssResolutionOverride() const;
		void setDlssAutoExposure(bool v);
		bool getDlssAutoExposure() const;
		bool getDlssInitialized() const;
#endif
	};
};