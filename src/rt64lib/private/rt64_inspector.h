//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Inspector {
	private:
		Device* device;
		ID3D12DescriptorHeap* d3dSrvDescHeap;
		RT64_MATERIAL* material;
		RT64_LIGHT* lights;
		int *lightCount;
		int maxLightCount;
		std::vector<std::string> toPrint;

		void renderMaterialInspector();
		void renderLightInspector();
		void renderPrint();
	public:
		Inspector(Device* device);
		~Inspector();
		void reset();
		void render();
		void resize();
		void setMaterial(RT64_MATERIAL *material);
		void setLights(RT64_LIGHT *lights, int *lightCount, int maxLightCount);
		void print(const std::string& message);
		bool handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
	};
};