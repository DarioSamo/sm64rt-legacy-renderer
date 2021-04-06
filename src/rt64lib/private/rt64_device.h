//
// RT64
//

#pragma once

#include "rt64_common.h"

#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"

namespace RT64 {
	class Scene;
	class Inspector;

	class Device {
	private:
		static const UINT FrameCount = 2;

		HWND hwnd;
		int width;
		int height;
		float aspectRatio;
		std::vector<Scene*> scenes;
		std::vector<Inspector*> inspectors;

		CD3DX12_VIEWPORT d3dViewport;
		CD3DX12_RECT d3dScissorRect;
		UINT d3dFrameIndex;
		HANDLE d3dFenceEvent;
		ID3D12Fence *d3dFence;
		UINT64 d3dFenceValue;
		ID3D12Device5 *d3dDevice;
		D3D12MA::Allocator *d3dAllocator;
		ID3D12CommandQueue *d3dCommandQueue;
		ID3D12GraphicsCommandList4 *d3dCommandList;
		IDXGISwapChain3 *d3dSwapChain;
		ID3D12Resource *d3dRenderTargets[FrameCount];
		ID3D12CommandAllocator *d3dCommandAllocator;
		ID3D12RootSignature *d3dRootSignature;
		ID3D12DescriptorHeap *d3dRtvHeap;
		ID3D12PipelineState *d3dPipelineState;
		ID3D12PipelineState *d3dComputeState;
		ID3D12DescriptorHeap *d3dDsvHeap;
		UINT d3dRtvDescriptorSize;
		IDxcBlob *d3dTracerLibrary;
		IDxcBlob *d3dSurfaceLibrary;
		IDxcBlob *d3dShadowLibrary;
		ID3D12RootSignature *d3dTracerSignature;
		ID3D12RootSignature *d3dSurfaceShadowSignature;
		ID3D12StateObject *d3dRtStateObject;
		ID3D12StateObjectProperties *d3dRtStateObjectProps;
		D3D12_RESOURCE_BARRIER lastCommandQueueBarrier;
		bool lastCommandQueueBarrierActive;
		D3D12_RESOURCE_BARRIER lastCopyQueueBarrier;
		bool lastCopyQueueBarrierActive;
		bool d3dCommandListOpen;

		void updateSize();
		void releaseRTVs();
		void createRTVs();
		void loadPipeline();
		void loadAssets();
		void checkRaytracingSupport();
		void createRaytracingPipeline();
		ID3D12RootSignature *createTracerSignature();
		ID3D12RootSignature *createSurfaceShadowSignature();
		void getHardwareAdapter(_In_ IDXGIFactory2 *pFactory, _Outptr_result_maybenull_ IDXGIAdapter1 **ppAdapter);
		void preRender();
		void postRender(int vsyncInterval);
	public:
		Device(HWND hwnd);
		virtual ~Device();
		void draw(int vsyncInterval);
		void addScene(Scene *scene);
		void removeScene(Scene *scene);
		void addInspector(Inspector* inspector);
		void removeInspector(Inspector* inspector);
		HWND getHwnd() const;
		ID3D12Device5 *getD3D12Device();
		ID3D12GraphicsCommandList4 *getD3D12CommandList();
		ID3D12StateObject *getD3D12RtStateObject();
		ID3D12StateObjectProperties *getD3D12RtStateObjectProperties();
		ID3D12Resource *getD3D12RenderTarget();
		ID3D12RootSignature* getD3D12RootSignature();
		ID3D12PipelineState *getD3D12PipelineState();
		ID3D12PipelineState* getD3D12ComputePipelineState();
		CD3DX12_VIEWPORT getD3D12Viewport();
		CD3DX12_RECT getD3D12ScissorRect(); 
		AllocatedResource allocateResource(D3D12_HEAP_TYPE HeapType, _In_  const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, _In_opt_  const D3D12_CLEAR_VALUE *pOptimizedClearValue);
		AllocatedResource allocateBuffer(D3D12_HEAP_TYPE HeapType, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES InitialResourceState);
		void setLastCommandQueueBarrier(const D3D12_RESOURCE_BARRIER &barrier);
		void submitCommandQueueBarrier();
		void setLastCopyQueueBarrier(const D3D12_RESOURCE_BARRIER &barrier);
		void submitCopyQueueBarrier();
		int getWidth() const;
		int getHeight() const;
		float getAspectRatio() const;
		void resetCommandList();
		void submitCommandList();
		void waitForGPU();
	};
};