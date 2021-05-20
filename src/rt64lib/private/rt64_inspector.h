//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;
	class Scene;
	class View;

	class Inspector {
	private:
		Device* device;
		ID3D12DescriptorHeap* d3dSrvDescHeap;
		RT64_MATERIAL* material;
		std::string materialName;
		RT64_LIGHT* lights;
		int *lightCount;
		int maxLightCount;
		bool cameraControl;
		float cameraPanX;
		float cameraPanY;
		int prevCursorX, prevCursorY;
		std::string dumpPath;
		int dumpFrameCount;
		std::vector<std::string> toPrint;

		void setupWithView(View *view, int cursorX, int cursorY);
		void renderViewParams(View *view, bool &rtStateDirty);
		void renderMaterialInspector();
		void renderLightInspector();
		void renderPrint();
		void renderCameraControl(View *view, int cursorX, int cursorY);
	public:
		Inspector(Device* device);
		~Inspector();
		void reset();
		void render(View *activeView, int cursorX, int cursorY, bool &rtStateDirty);
		void resize();
		void setMaterial(RT64_MATERIAL *material, const std::string& materialName);
		void setLights(RT64_LIGHT *lights, int *lightCount, int maxLightCount);
		void print(const std::string& message);
		bool handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
	};
};