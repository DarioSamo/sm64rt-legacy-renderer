//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;
	class Instance;
	class View;

	class Scene {
	private:
		Device *device;
		std::vector<Instance *> instances;
		std::vector<View *> views;
		AllocatedResource lightsBuffer;
		size_t lightsBufferSize;
		int lightsCount;
	public:
		Scene(Device *device);
		virtual ~Scene();
		void update();
		void render();
		void resize();
		void setLights(RT64_LIGHT *lightArray, int lightCount);
		int getLightsCount() const;
		ID3D12Resource *getLightsBuffer() const;
		void addInstance(Instance *instance);
		void removeInstance(Instance *instance);
		void addView(View *view);
		void removeView(View *view);
		const std::vector<Instance *> &getInstances() const;
		Device *getDevice() const;
	};
};