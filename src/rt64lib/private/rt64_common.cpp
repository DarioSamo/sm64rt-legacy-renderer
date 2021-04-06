//
// RT64
//

#include "rt64_common.h"

namespace nv_helpers_dx12
{
	ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible) {
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = count;
		desc.Type = type;
		desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		ID3D12DescriptorHeap* pHeap;
		ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)));
		return pHeap;
	}
}