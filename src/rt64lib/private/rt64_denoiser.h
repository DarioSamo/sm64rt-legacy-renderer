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
		void denoise();
		void resize(unsigned int width, unsigned int height);
		ID3D12Resource *getColor() const;
		ID3D12Resource *getAlbedo() const;
		ID3D12Resource *getNormal() const;
		ID3D12Resource *getOutput() const;
	};
};