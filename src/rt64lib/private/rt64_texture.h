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
		DXGI_FORMAT format;
		int currentIndex;

		void setRawWithFormat(DXGI_FORMAT format, const void *bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps);
	public:
		Texture(Device *device);
		virtual ~Texture();
		void setRGBA8(const void *bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps);
		void setDDS(const void *bytes, int byteCount);
		ID3D12Resource *getTexture() const;
		DXGI_FORMAT getFormat() const;
		void setCurrentIndex(int v);
		int getCurrentIndex() const;
	};
};