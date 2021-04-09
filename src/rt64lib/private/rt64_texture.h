//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Texture {
	private:
		Device *device;
		AllocatedResource texture;
		AllocatedResource textureUpload;
	public:
		Texture(Device *device, const void *bytes, int width, int height, int stride);
		virtual ~Texture();
		ID3D12Resource *getTexture();
	};
};