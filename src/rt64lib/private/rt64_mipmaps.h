//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Mipmaps {
	private:
		Device *device;
		ID3D12RootSignature *d3dRootSignature;
		ID3D12PipelineState *d3dPipelineState;
		ID3D12DescriptorHeap *d3dDescriptorHeaps[8];
	public:
		Mipmaps(Device *device);
		void generate(ID3D12Resource *sourceTexture);
	};
};

#endif