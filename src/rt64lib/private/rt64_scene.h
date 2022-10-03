//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;
	class Inspector;
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
		RT64_SCENE_DESC description;
	public:
		Scene(Device *device);
		virtual ~Scene();
		void update();
		void render(float deltaTimeMs);
		void resize();
		void setDescription(RT64_SCENE_DESC v);
		RT64_SCENE_DESC getDescription() const;
		void setLights(RT64_LIGHT *lightArray, int lightCount);
		int getLightsCount() const;
		ID3D12Resource *getLightsBuffer() const;
		void addInstance(Instance *instance);
		void removeInstance(Instance *instance);
		void addView(View *view);
		void removeView(View *view);
		const std::vector<View *> &getViews() const;
		const std::vector<Instance *> &getInstances() const;
		Device *getDevice() const;
	};
};