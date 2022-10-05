//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_xess.h"

#include "xess/inc/xess/xess.h"
#include "xess/inc/xess/xess_d3d12.h"
#include "xess/inc/xess/xess_debug.h"
#include "xess/inc/xess/xess_d3d12_debug.h"

#include "rt64_device.h"

// XeSS::Context

class RT64::XeSS::Context {
private:
    static const uint32_t XeSSFlags = XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK | XESS_INIT_FLAG_LDR_INPUT_COLOR;

    Device *device;
    bool initialized;
    xess_context_handle_t xessContext;
    xess_version_t xefxVersion;
public:
    Context(Device *device) {
        assert(device != nullptr);

        this->device = device;
        initialized = false;

        xess_result_t xessRes = xessD3D12CreateContext(device->getD3D12Device(), &xessContext);
        if (xessRes != XESS_RESULT_SUCCESS) {
            RT64_LOG_PRINTF("xessD3D12CreateContext failed: %d\n", xessRes);
            return;
        }

        xessRes = xessGetIntelXeFXVersion(xessContext, &xefxVersion);
        if (xessRes != XESS_RESULT_SUCCESS) {
            RT64_LOG_PRINTF("xessGetIntelXeFXVersion failed: %d\n", xessRes);
            return;
        }

        xessRes = xessD3D12BuildPipelines(xessContext, nullptr, false, XeSSFlags);
        if (xessRes != XESS_RESULT_SUCCESS) {
            RT64_LOG_PRINTF("xessD3D12BuildPipelines failed: %d\n", xessRes);
            return;
        }

        initialized = true;

        xessSetVelocityScale(xessContext, -1.0f, -1.0f);
    }

    ~Context() {
        if (initialized) {
            device->waitForGPU();
            xessDestroyContext(xessContext);
        }
    }
    
    xess_quality_settings_t toXeSSQuality(QualityMode q) {
        switch (q) {
        case QualityMode::UltraPerformance:
        case QualityMode::Performance:
            return XESS_QUALITY_SETTING_PERFORMANCE;
        case QualityMode::Balanced:
            return XESS_QUALITY_SETTING_BALANCED;
        case QualityMode::Quality:
            return XESS_QUALITY_SETTING_QUALITY;
        case QualityMode::UltraQuality:
        case QualityMode::Native:
            return XESS_QUALITY_SETTING_ULTRA_QUALITY;
        default:
            return XESS_QUALITY_SETTING_BALANCED;
        }
    }
    
    bool set(QualityMode quality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
        if (quality == QualityMode::Auto) {
            quality = getQualityAuto(displayWidth, displayHeight);
        }

        xess_d3d12_init_params_t initParams = { };
        initParams.outputResolution.x = displayWidth;
        initParams.outputResolution.y = displayHeight;
        initParams.qualitySetting = toXeSSQuality(quality);
        initParams.initFlags = XeSSFlags;

        xess_result_t xessRes = xessD3D12Init(xessContext, &initParams);
        if (xessRes != XESS_RESULT_SUCCESS) {
            RT64_LOG_PRINTF("xessD3D12Init failed: %d\n", xessRes);
            return false;
        }

        return true;
    }

    bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
        if (quality == QualityMode::Auto) {
            quality = getQualityAuto(displayWidth, displayHeight);
        }

        xess_2d_t outputResolution, internalResolution;
        outputResolution.x = displayWidth;
        outputResolution.y = displayHeight;

        // XeSS doesn't provide these quality settings, so we force them instead.
        if (quality == QualityMode::Native) {
            renderWidth = displayWidth;
            renderHeight = displayHeight;
        }
        else if (quality == QualityMode::UltraPerformance) {
            renderWidth = displayWidth / 3;
            renderHeight = displayHeight / 3;
        }
        else {
            xess_result_t xessRes = xessGetInputResolution(xessContext, &outputResolution, toXeSSQuality(quality), &internalResolution);
            if (xessRes != XESS_RESULT_SUCCESS) {
                RT64_LOG_PRINTF("xessGetInputResolution failed: %d\n", xessRes);
                return false;
            }

            renderWidth = internalResolution.x;
            renderHeight = internalResolution.y;
        }

        return true;
    }

    uint32_t getJitterPhaseCount(int renderWidth, int displayWidth) {
        // FIXME: The XeSS documentation doesn't seem to specify a particular phase count
        // or a formula to compute it. We use the recommended one by NVIDIA for DLSS.
        return 64;
    }

    void upscale(const UpscaleParameters &p) {
        xess_d3d12_execute_params_t execParams = { };
        execParams.pColorTexture = p.inColor;
        execParams.pVelocityTexture = p.inFlow;
        execParams.pDepthTexture = p.inDepth;
        execParams.pExposureScaleTexture = nullptr;
        execParams.pResponsivePixelMaskTexture = p.inLockMask;
        execParams.pOutputTexture = p.outColor;
        execParams.jitterOffsetX = p.jitterX;
        execParams.jitterOffsetY = p.jitterY;
        execParams.exposureScale = 1.0f;
        execParams.resetHistory = p.resetAccumulation;
        execParams.inputWidth = p.inRect.w;
        execParams.inputHeight = p.inRect.h;

        xess_result_t xessRes = xessD3D12Execute(xessContext, device->getD3D12CommandList(), &execParams);
        if (xessRes != XESS_RESULT_SUCCESS) {
            RT64_LOG_PRINTF("xessD3D12Execute failed: %d\n", xessRes);
            return;
        }
    }

    bool isInitialized() const {
        return initialized;
    }

    bool isAccelerated() const {
        return (xefxVersion.major != 0) || (xefxVersion.minor != 0) || (xefxVersion.patch != 0);
    }
};

// XeSS

RT64::XeSS::XeSS(Device *device) {
    ctx = new Context(device);
}

RT64::XeSS::~XeSS() {
    delete ctx;
}

bool RT64::XeSS::isAccelerated() const {
    return ctx->isAccelerated();
}

void RT64::XeSS::set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight) {
    ctx->set(inQuality, renderWidth, renderHeight, displayWidth, displayHeight);
}

bool RT64::XeSS::getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight) {
    return ctx->getQualityInformation(quality, displayWidth, displayHeight, renderWidth, renderHeight);
}

int RT64::XeSS::getJitterPhaseCount(int renderWidth, int displayWidth) {
    return ctx->getJitterPhaseCount(renderWidth, displayWidth);
}

void RT64::XeSS::upscale(const UpscaleParameters &p) {
    ctx->upscale(p);
}

bool RT64::XeSS::isInitialized() const {
    return ctx->isInitialized();
}

bool RT64::XeSS::requiresNonShaderResourceInputs() const {
    return true;
}

#endif