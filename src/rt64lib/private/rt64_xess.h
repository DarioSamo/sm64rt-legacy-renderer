//
// RT64
//

#pragma once

#include "rt64_upscaler.h"

namespace RT64 {
	class Device;

	class XeSS : public Upscaler {
	private:
		class Context;
		Context *ctx;
	public:
		XeSS(Device *device);
		~XeSS();
		bool isAccelerated() const;
		virtual void set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) override;
		virtual bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) override;
		virtual int getJitterPhaseCount(int renderWidth, int displayWidth) override;
		virtual void upscale(const UpscaleParameters &p) override;
		virtual bool isInitialized() const override;
		virtual bool requiresNonShaderResourceInputs() const override;
	};
};