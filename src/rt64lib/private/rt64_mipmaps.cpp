//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_mipmaps.h"

#include "shaders/GenerateMipsCS.hlsl.h"

#include "nv_helpers_dx12/RootSignatureGenerator.h"

#include "rt64_device.h"

struct alignas(16) GenerateMipsCB {
	uint32_t SrcMipLevel;           // Texture level of source mip
	uint32_t NumMipLevels;          // Number of OutMips to write: [1-4]
	uint32_t SrcDimension;          // Width and height of the source texture are even or odd.
	uint32_t IsSRGB;                // Must apply gamma correction to sRGB textures.
	DirectX::XMFLOAT2 TexelSize;    // 1.0 / OutMip1.Dimensions
};

RT64::Mipmaps::Mipmaps(Device *device) {
	assert(device != nullptr);

	this->device = device;
	auto d3dDevice = device->getD3D12Device();
	memset(d3dDescriptorHeaps, 0, sizeof(d3dDescriptorHeaps));

	RT64_LOG_PRINTF("Creating the generate mipmaps root signature");
	{
		nv_helpers_dx12::RootSignatureGenerator rsc;
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, 0, 0, 6);
		rsc.AddHeapRangesParameter({
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 },
			{ 0, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1 },
			{ 1, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2 },
			{ 2, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3 },
			{ 3, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4 }
		});

		// Fill out the sampler.
		D3D12_STATIC_SAMPLER_DESC desc = { };
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		desc.MaxAnisotropy = 1;
		desc.MaxLOD = D3D12_FLOAT32_MAX;

		d3dRootSignature = rsc.Generate(d3dDevice, false, true, &desc, 1);
	}

	RT64_LOG_PRINTF("Creating the generate mipmaps pipeline state");
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(GenerateMipsCSBlob, sizeof(GenerateMipsCSBlob));
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoDesc.pRootSignature = d3dRootSignature;
		psoDesc.NodeMask = 0;

		D3D12_CHECK(d3dDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&d3dPipelineState)));
	}
}

void RT64::Mipmaps::generate(ID3D12Resource *resource) {
	assert(resource != nullptr);

	auto d3dDevice = device->getD3D12Device();
	auto d3dCommandList = device->getD3D12CommandList();
	auto origDesc = resource->GetDesc();
	
	// If the texture only has a single mip level (level 0) do nothing.
	if (origDesc.MipLevels == 1) {
		return;
	}

	// Currently, only non-multi-sampled 2D textures are supported.
	if (origDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		origDesc.DepthOrArraySize != 1 ||
		origDesc.SampleDesc.Count > 1)
	{
		throw std::exception("GenerateMips is only supported for non-multi-sampled 2D Textures.");
	}

	ID3D12Heap *aliasHeap = nullptr;
	ID3D12Resource *uavResource = resource;
	ID3D12Resource *aliasResource = nullptr;

	// If the passed-in resource does not allow for UAV access then create a staging resource that is used to generate the mipmap chain.
	if ((origDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
		// Describe an alias resource that is used to copy the original texture.
		auto aliasDesc = origDesc;

		// Placed resources can't be render targets or depth-stencil views.
		aliasDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		aliasDesc.Flags &= ~(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		// Describe a UAV compatible resource that is used to perform mipmapping of the original texture.
		auto uavDesc = aliasDesc;
		uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		// Create a heap that is large enough to store a copy of the original resource.
		D3D12_RESOURCE_DESC resourceDescs[] = { aliasDesc, uavDesc };
		auto allocationInfo = d3dDevice->GetResourceAllocationInfo(0, _countof(resourceDescs), resourceDescs);

		D3D12_HEAP_DESC heapDesc = {};
		heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
		heapDesc.Alignment = allocationInfo.Alignment;
		heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_CHECK(d3dDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(&aliasHeap)));

		// Create a placed resource that matches the description of the original resource.
		// This resource is used to copy the original texture to the UAV compatible resource.
		D3D12_CHECK(d3dDevice->CreatePlacedResource(aliasHeap, 0, &aliasDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&aliasResource)));
		aliasResource->SetName(L"aliasResource");

		// Create a UAV compatible resource in the same heap as the alias resource.
		D3D12_CHECK(d3dDevice->CreatePlacedResource(aliasHeap, 0, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&uavResource)));
		uavResource->SetName(L"uavResource");

		// Copy the resource into the alias resource.
		CD3DX12_RESOURCE_BARRIER beforeCopyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
		d3dCommandList->ResourceBarrier(1, &beforeCopyBarrier);

		d3dCommandList->CopyResource(aliasResource, resource);

		CD3DX12_RESOURCE_BARRIER afterCopyBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(aliasResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE)
		};

		d3dCommandList->ResourceBarrier(_countof(afterCopyBarriers), afterCopyBarriers);
	}
	else {
		CD3DX12_RESOURCE_BARRIER beforeGenerationBarrier = CD3DX12_RESOURCE_BARRIER::Transition(uavResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		d3dCommandList->ResourceBarrier(1, &beforeGenerationBarrier);
	}

	d3dCommandList->SetPipelineState(d3dPipelineState);
	d3dCommandList->SetComputeRootSignature(d3dRootSignature);

	GenerateMipsCB cb;
	cb.IsSRGB = false;
	auto resourceDesc = uavResource->GetDesc();

	uint32_t heapIndex = 0;
	for (uint32_t srcMip = 0; srcMip < resourceDesc.MipLevels - 1u; ) {
		uint64_t srcWidth = resourceDesc.Width >> srcMip;
		uint32_t srcHeight = resourceDesc.Height >> srcMip;
		uint32_t dstWidth = static_cast<uint32_t>(srcWidth >> 1);
		uint32_t dstHeight = srcHeight >> 1;

		// 0b00(0): Both width and height are even.
		// 0b01(1): Width is odd, height is even.
		// 0b10(2): Width is even, height is odd.
		// 0b11(3): Both width and height are odd.
		cb.SrcDimension = (srcHeight & 1) << 1 | (srcWidth & 1);

		// How many mipmap levels to compute this pass (max 4 mips per pass)
		DWORD mipCount;

		// The number of times we can half the size of the texture and get
		// exactly a 50% reduction in size.
		// A 1 bit in the width or height indicates an odd dimension.
		// The case where either the width or the height is exactly 1 is handled
		// as a special case (as the dimension does not require reduction).
		_BitScanForward(&mipCount, (dstWidth == 1 ? dstHeight : dstWidth) | (dstHeight == 1 ? dstWidth : dstHeight));

		// Maximum number of mips to generate is 4.
		mipCount = std::min<DWORD>(4, mipCount + 1);

		// Clamp to total number of mips left over.
		mipCount = (srcMip + mipCount) >= resourceDesc.MipLevels ?
			resourceDesc.MipLevels - srcMip - 1 : mipCount;

		// Dimensions should not reduce to 0.
		// This can happen if the width and height are not the same.
		dstWidth = std::max<DWORD>(1, dstWidth);
		dstHeight = std::max<DWORD>(1, dstHeight);

		cb.SrcMipLevel = srcMip;
		cb.NumMipLevels = mipCount;
		cb.TexelSize.x = 1.0f / (float)dstWidth;
		cb.TexelSize.y = 1.0f / (float)dstHeight;

		// Update the descriptor heap.
		if (d3dDescriptorHeaps[heapIndex] == nullptr) {
			uint32_t handleCount = 6;
			d3dDescriptorHeaps[heapIndex] = nv_helpers_dx12::CreateDescriptorHeap(d3dDevice, handleCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		}

		// SRV for source mip.
		D3D12_CPU_DESCRIPTOR_HANDLE handle = d3dDescriptorHeaps[heapIndex]->GetCPUDescriptorHandleForHeapStart();
		const UINT handleIncrement = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
		textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		textureSRVDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
		textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d3dDevice->CreateShaderResourceView(uavResource, &textureSRVDesc, handle);
		handle.ptr += handleIncrement;

		// UAVs for destination mips.
		for (uint32_t mip = 0; mip < 4; ++mip) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = resourceDesc.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = srcMip + mip + 1;
			d3dDevice->CreateUnorderedAccessView((mip < mipCount) ? uavResource : nullptr, nullptr, &uavDesc, handle);
			handle.ptr += handleIncrement;
		}

		// Transition the source mipmap to a shader resource.
		CD3DX12_RESOURCE_BARRIER beforeDispatchBarrier = CD3DX12_RESOURCE_BARRIER::Transition(uavResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcMip);
		d3dCommandList->ResourceBarrier(1, &beforeDispatchBarrier);

		d3dCommandList->SetComputeRoot32BitConstants(0, 6, &cb, 0);
		std::vector<ID3D12DescriptorHeap *> descriptorHeaps = { d3dDescriptorHeaps[heapIndex] };
		d3dCommandList->SetDescriptorHeaps(static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());
		d3dCommandList->SetComputeRootDescriptorTable(1, d3dDescriptorHeaps[heapIndex]->GetGPUDescriptorHandleForHeapStart());

		// Dispatch the compute shader.
		UINT threadXCount = dstWidth / 8 + ((dstWidth % 8) ? 1 : 0);
		UINT threadYCount = dstHeight / 8 + ((dstHeight % 8) ? 1 : 0);
		d3dCommandList->Dispatch(threadXCount, threadYCount, 1);

		// Ensure UAVs are written to before the next batch of mipmaps.
		CD3DX12_RESOURCE_BARRIER afterDispatchBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(uavResource),
			CD3DX12_RESOURCE_BARRIER::Transition(uavResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, srcMip)
		};

		d3dCommandList->ResourceBarrier(_countof(afterDispatchBarriers), afterDispatchBarriers);

		srcMip += mipCount;
		heapIndex++;
	}
	
	if (aliasResource != nullptr) {
		d3dCommandList->CopyResource(resource, aliasResource);

		CD3DX12_RESOURCE_BARRIER afterCopyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &afterCopyBarrier);
	}
	else {
		CD3DX12_RESOURCE_BARRIER afterGenerationBarrier = CD3DX12_RESOURCE_BARRIER::Transition(uavResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &afterGenerationBarrier);
	}

	device->submitCommandList();
	device->waitForGPU();
	device->resetCommandList();

	// Release all resources that were used to create the alias heap.
	// TODO: Maybe queue these resources for deletion instead so we don't need to execute the command list instantly.
	if (aliasResource != nullptr) {
		aliasResource->Release();
		uavResource->Release();
		aliasHeap->Release();
	}
}

#endif