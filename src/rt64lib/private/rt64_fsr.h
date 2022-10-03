//
// RT64
//

#pragma once
#include "rt64_common.h"

namespace RT64 {
	class Device;

	class FSR {
	public:
		enum class QualityMode : int {
			UltraPerformance = 0,
			Performance,
			Balanced,
			Quality,
			Auto,
			MAX
		};

		struct UpscaleParameters {
			RT64_RECT inRect;
			ID3D12Resource *inColor;
			ID3D12Resource *inFlow;
			ID3D12Resource *inDepth;
			ID3D12Resource *outColor;
			float jitterX;
			float jitterY;
			float sharpness;
			float deltaTime;
			float nearPlane;
			float farPlane;
			float fovY;
			bool resetAccumulation;
		};
	private:
		class Context;
		Context *ctx;
	public:
		FSR(Device *device);
		~FSR();
		void set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight);
		bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight);
		int getJitterPhaseCount(int renderWidth, int displayWidth);
		void upscale(const UpscaleParameters &p);
		bool isInitialized() const;
	};
};