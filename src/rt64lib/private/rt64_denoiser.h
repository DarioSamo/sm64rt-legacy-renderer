//
// RT64
//

#pragma once

#include "rt64_common.h"

// The latest NVIDIA drivers have an odd issue where the same thread that creates a D3D12 state object will
// crash if any OptiX routine is called afterwards. The solution is to create a dedicated thread that performs
// all the OptiX operations. Since this logic is unnecessarily complex for what the class intends to do and it 
// is likely to be fixed in a future driver update, this macro hides behind it a more straightforward implementation.

#define DENOISER_THREAD_CRASH_WORKAROUND

namespace RT64 {
	class Device;

	class Denoiser {
	private:
		class Context;
		Context *ctx;
#ifdef DENOISER_THREAD_CRASH_WORKAROUND
		class WorkerThread;
		WorkerThread *thread;
#endif
	public:
		Denoiser(Device *device, bool temporal);
		virtual ~Denoiser();

		// Requires R32G32B32A32 textures.
		void set(unsigned int width, unsigned int height, ID3D12Resource *inOutColor[2], ID3D12Resource *inAlbedo, ID3D12Resource *inNormal, ID3D12Resource *inFlow);
		void denoise(bool swapImages);
		bool isTemporal() const;
	};
};