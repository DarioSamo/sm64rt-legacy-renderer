//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class DLSS {
	private:
		class Context;
		Context *ctx;
	public:
		DLSS(Device *device);
		~DLSS();
		void set(int inputWidth, int inputHeight, int outputWidth, int outputHeight);
		void upscale(
			unsigned int inColorOffX, unsigned int inColorOffY,
			unsigned int inColorWidth, unsigned int inColorHeight,
			ID3D12Resource *inColor, ID3D12Resource *outColor, ID3D12Resource *inFlow, ID3D12Resource *inDepth,
			bool resetAccumulation, float jitterOffX, float jitterOffY);
	};
};