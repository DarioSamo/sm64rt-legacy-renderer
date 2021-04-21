//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Denoiser {
	private:
		class Context;
		Context *ctx;
	public:
		Denoiser(Device *device);
		virtual ~Denoiser();

		// Requires R32G32B32A32 textures.
		void set(unsigned int width, unsigned int height, ID3D12Resource *inOutColor, ID3D12Resource *inAlbedo, ID3D12Resource *inNormal);
		void denoise();
	};
};