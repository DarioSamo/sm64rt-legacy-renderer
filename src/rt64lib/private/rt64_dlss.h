//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class DLSS {
	public:
		enum class QualityMode : int {
			UltraPerformance = 0,
			MaxPerformance,
			Balanced,
			MaxQuality,
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
			bool resetAccumulation;
		};
	private:
		class Context;
		Context *ctx;
	public:
		DLSS(Device *device);
		~DLSS();
		void set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight, bool autoExposure);
		bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight, float &sharpness);
		void upscale(const UpscaleParameters &p);
		bool isInitialized() const;
	};
};