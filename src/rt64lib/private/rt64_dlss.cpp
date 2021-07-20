//
// RT64
//

#ifndef RT64_MINIMAL

#ifdef RT64_DLSS

#include "rt64_dlss.h"

#include <nvsdk_ngx_helpers.h>

#include "rt64_common.h"
#include "rt64_device.h"

#define APP_ID 0x4D4152494F3634

class RT64::DLSS::Context {
private:
    Device *device;
	NVSDK_NGX_Parameter *ngxParameters = nullptr;
	NVSDK_NGX_Handle *dlssFeature = nullptr;
    bool initialized = false;
public:
	Context(Device *device) {
        assert(device != nullptr);
        this->device = device;

		ID3D12Device *d3dDevice = device->getD3D12Device();
		NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init(APP_ID, L"./", d3dDevice);
		if (NVSDK_NGX_FAILED(result)) {
			RT64_LOG_PRINTF("NVSDK_NGX_D3D12_Init failed: %ls\n", GetNGXResultAsString(result));
			return;
		}

		result = NVSDK_NGX_D3D12_GetCapabilityParameters(&ngxParameters);
		if (NVSDK_NGX_FAILED(result)) {
			RT64_LOG_PRINTF("NVSDK_NGX_GetCapabilityParameters failed: %ls\n", GetNGXResultAsString(result));
			return;
		}

		// Check for driver version.
		int needsUpdatedDriver = 0;
		unsigned int minDriverVersionMajor = 0;
		unsigned int minDriverVersionMinor = 0;
		NVSDK_NGX_Result ResultUpdatedDriver = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver);
		NVSDK_NGX_Result ResultMinDriverVersionMajor = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverVersionMajor);
		NVSDK_NGX_Result ResultMinDriverVersionMinor = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverVersionMinor);
		if (ResultUpdatedDriver == NVSDK_NGX_Result_Success && ResultMinDriverVersionMajor == NVSDK_NGX_Result_Success && ResultMinDriverVersionMinor == NVSDK_NGX_Result_Success) {
			if (needsUpdatedDriver) {
				RT64_LOG_PRINTF("NVIDIA DLSS cannot be loaded due to outdated driver. Minimum Driver Version required : %u.%u", minDriverVersionMajor, minDriverVersionMinor);
				return;
			}
			else {
				RT64_LOG_PRINTF("NVIDIA DLSS Minimum driver version was reported as: %u.%u", minDriverVersionMajor, minDriverVersionMinor);
			}
		}
		else {
			RT64_LOG_PRINTF("NVIDIA DLSS Minimum driver version was not reported.");
		}

		// Check if DLSS is available.
		int dlssAvailable = 0;
		NVSDK_NGX_Result ResultDLSS = ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
		if (ResultDLSS != NVSDK_NGX_Result_Success || !dlssAvailable) {
			NVSDK_NGX_Result FeatureInitResult = NVSDK_NGX_Result_Fail;
			NVSDK_NGX_Parameter_GetI(ngxParameters, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, (int *)&FeatureInitResult);
			RT64_LOG_PRINTF("NVIDIA DLSS not available on this hardware/platform., FeatureInitResult = 0x%08x, info: %ls", FeatureInitResult, GetNGXResultAsString(FeatureInitResult));
			return;
		}

        initialized = true;
	}

	~Context() {
        release();
        NVSDK_NGX_D3D12_Shutdown();
	}

    NVSDK_NGX_PerfQuality_Value toNGXQuality(QualityMode q) {
        switch (q) {
        case QualityMode::UltraPerformance:
            return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        case QualityMode::MaxPerformance:
            return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case QualityMode::Balanced:
            return NVSDK_NGX_PerfQuality_Value_Balanced;
        case QualityMode::MaxQuality:
            return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        default:
            return NVSDK_NGX_PerfQuality_Value_Balanced;
        }
    }

    bool set(QualityMode quality, int renderWidth, int renderHeight, int displayWidth, int displayHeight, bool autoExposure) {
        release();

        unsigned int CreationNodeMask = 1;
        unsigned int VisibilityNodeMask = 1;
        NVSDK_NGX_Result ResultDLSS = NVSDK_NGX_Result_Fail;
        int DlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
        DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_DoSharpening;

        if (autoExposure) {
            DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
            DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        }

        NVSDK_NGX_DLSS_Create_Params DlssCreateParams;
        memset(&DlssCreateParams, 0, sizeof(DlssCreateParams));
        DlssCreateParams.Feature.InWidth = renderWidth;
        DlssCreateParams.Feature.InHeight = renderHeight;
        DlssCreateParams.Feature.InTargetWidth = displayWidth;
        DlssCreateParams.Feature.InTargetHeight = displayHeight;
        DlssCreateParams.Feature.InPerfQualityValue = toNGXQuality(quality);
        DlssCreateParams.InFeatureCreateFlags = DlssCreateFeatureFlags;

        auto d3dCommandList = device->getD3D12CommandList();
        ResultDLSS = NGX_D3D12_CREATE_DLSS_EXT(d3dCommandList, CreationNodeMask, VisibilityNodeMask, &dlssFeature, ngxParameters, &DlssCreateParams);

        device->submitCommandList();
        device->waitForGPU();
        device->resetCommandList();

        if (NVSDK_NGX_FAILED(ResultDLSS)) {
            RT64_LOG_PRINTF("Failed to create DLSS Features = 0x%08x, info: %ls", ResultDLSS, GetNGXResultAsString(ResultDLSS));
            return false;
        }

        return true;
    }

    void release() {
        device->waitForGPU();

        // Try to release the DLSS feature.
        NVSDK_NGX_Result ResultDLSS = (dlssFeature != nullptr) ? NVSDK_NGX_D3D12_ReleaseFeature(dlssFeature) : NVSDK_NGX_Result_Success;
        if (NVSDK_NGX_FAILED(ResultDLSS)) {
            RT64_LOG_PRINTF("Failed to NVSDK_NGX_D3D12_ReleaseFeature, code = 0x%08x, info: %ls", ResultDLSS, GetNGXResultAsString(ResultDLSS));
        }

        dlssFeature = nullptr;
    }

    bool getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight, float &sharpness) {
        unsigned int renderOptimalWidth = 0, renderOptimalHeight = 0;
        unsigned int renderMaxWidth = 0, renderMaxHeight = 0;
        unsigned int renderMinWidth = 0, renderMinHeight = 0;
        float renderSharpness;
        NVSDK_NGX_Result Result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
            ngxParameters,
            displayWidth, displayHeight,
            toNGXQuality(quality),
            &renderOptimalWidth, &renderOptimalHeight,
            &renderMaxWidth, &renderMaxHeight,
            &renderMinWidth, &renderMinHeight,
            &renderSharpness);

        // Failed to retrieve the optimal settings.
        if (NVSDK_NGX_FAILED(Result)) {
            RT64_LOG_PRINTF("Querying Optimal Settings failed! code = 0x%08x, info: %ls", Result, GetNGXResultAsString(Result));
            return false;
        }
        // Quality mode isn't allowed.
        else if ((renderOptimalWidth == 0) || (renderOptimalHeight == 0)) {
            return false;
        }
        else {
            renderWidth = renderOptimalWidth;
            renderHeight = renderOptimalHeight;
            sharpness = renderSharpness;
            return true;
        }
    }

    void upscale(const UpscaleParameters &p) {
        assert(dlssFeature != nullptr);
        int Reset = p.resetAccumulation ? 1 : 0;
        NVSDK_NGX_Coordinates renderingOffset = { (unsigned int)(p.inRect.x), (unsigned int)(p.inRect.y) };
        NVSDK_NGX_Dimensions  renderingSize = { (unsigned int)(p.inRect.w), (unsigned int)(p.inRect.h) };

        NVSDK_NGX_Result Result;
        NVSDK_NGX_D3D12_DLSS_Eval_Params D3D12DlssEvalParams;
        memset(&D3D12DlssEvalParams, 0, sizeof(D3D12DlssEvalParams));

        D3D12DlssEvalParams.Feature.pInColor = p.inColor;
        D3D12DlssEvalParams.Feature.pInOutput = p.outColor;
        D3D12DlssEvalParams.pInDepth = p.inDepth;
        D3D12DlssEvalParams.pInMotionVectors = p.inFlow;
        D3D12DlssEvalParams.pInExposureTexture = nullptr;
        D3D12DlssEvalParams.InJitterOffsetX = p.jitterX;
        D3D12DlssEvalParams.InJitterOffsetY = p.jitterY;
        D3D12DlssEvalParams.Feature.InSharpness = p.sharpness;
        D3D12DlssEvalParams.InReset = Reset;
        D3D12DlssEvalParams.InMVScaleX = 1.0f;
        D3D12DlssEvalParams.InMVScaleY = 1.0f;
        D3D12DlssEvalParams.InColorSubrectBase = renderingOffset;
        D3D12DlssEvalParams.InDepthSubrectBase = renderingOffset;
        D3D12DlssEvalParams.InTranslucencySubrectBase = renderingOffset;
        D3D12DlssEvalParams.InMVSubrectBase = renderingOffset;
        D3D12DlssEvalParams.InRenderSubrectDimensions = renderingSize;

        auto d3dCommandList = device->getD3D12CommandList();
        Result = NGX_D3D12_EVALUATE_DLSS_EXT(d3dCommandList, dlssFeature, ngxParameters, &D3D12DlssEvalParams);

        if (NVSDK_NGX_FAILED(Result)) {
            RT64_LOG_PRINTF("Failed to NVSDK_NGX_D3D12_EvaluateFeature for DLSS, code = 0x%08x, info: %ls", Result, GetNGXResultAsString(Result));
        }
    }

    bool isInitialized() const {
        return initialized;
    }
};

RT64::DLSS::DLSS(Device *device) {
	ctx = new Context(device);
}

RT64::DLSS::~DLSS() {
	delete ctx;
}

void RT64::DLSS::set(QualityMode inQuality, int renderWidth, int renderHeight, int displayWidth, int displayHeight, bool autoExposure) {
    ctx->set(inQuality, renderWidth, renderHeight, displayWidth, displayHeight, autoExposure);
}

bool RT64::DLSS::getQualityInformation(QualityMode quality, int displayWidth, int displayHeight, int &renderWidth, int &renderHeight, float &sharpness) {
    return ctx->getQualityInformation(quality, displayWidth, displayHeight, renderWidth, renderHeight, sharpness);
}

void RT64::DLSS::upscale(const UpscaleParameters &p) {
    ctx->upscale(p);
}

bool RT64::DLSS::isInitialized() const {
    return ctx->isInitialized();
}

#endif

#endif