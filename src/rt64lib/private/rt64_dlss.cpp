//
// RT64
//

#include "rt64_dlss.h"

#include <nvsdk_ngx_helpers.h>

#include "rt64_common.h"
#include "rt64_device.h"

// TODO: Change App Id to what exactly?
#define APP_ID 231313132

class RT64::DLSS::Context {
private:
    Device *device;
	NVSDK_NGX_Parameter *ngxParameters = nullptr;
	NVSDK_NGX_Handle *dlssFeature = nullptr;
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
	}

	~Context() {
        release();
        NVSDK_NGX_D3D12_Shutdown();
	}

    bool set(int inputWidth, int inputHeight, int outputWidth, int outputHeight) {
        release();

        // TODO: Check if NGX is initialized.
        unsigned int CreationNodeMask = 1;
        unsigned int VisibilityNodeMask = 1;
        NVSDK_NGX_Result ResultDLSS = NVSDK_NGX_Result_Fail;
        int DlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
        DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        //DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_DoSharpening;

        NVSDK_NGX_DLSS_Create_Params DlssCreateParams;
        memset(&DlssCreateParams, 0, sizeof(DlssCreateParams));
        DlssCreateParams.Feature.InWidth = inputWidth;
        DlssCreateParams.Feature.InHeight = inputHeight;
        DlssCreateParams.Feature.InTargetWidth = outputWidth;
        DlssCreateParams.Feature.InTargetHeight = outputHeight;
        //DlssCreateParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxPerf;
        DlssCreateParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_Balanced;
        //DlssCreateParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
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

    void upscale(
        unsigned int inColorOffX,
        unsigned int inColorOffY,
        unsigned int inColorWidth,
        unsigned int inColorHeight,
        ID3D12Resource *inColor,
        ID3D12Resource *outColor,
        ID3D12Resource *inFlow,
        ID3D12Resource *inDepth,
        bool resetAccumulation,
        float jitterOffX,
        float jitterOffY)
    {
        assert(dlssFeature != nullptr);
        int Reset = resetAccumulation ? 1 : 0;
        NVSDK_NGX_Coordinates renderingOffset = { inColorOffX, inColorOffY };
        NVSDK_NGX_Dimensions  renderingSize = { inColorWidth, inColorHeight };

        NVSDK_NGX_Result Result;
        NVSDK_NGX_D3D12_DLSS_Eval_Params D3D12DlssEvalParams;
        memset(&D3D12DlssEvalParams, 0, sizeof(D3D12DlssEvalParams));

        D3D12DlssEvalParams.Feature.pInColor = inColor;
        D3D12DlssEvalParams.Feature.pInOutput = outColor;
        D3D12DlssEvalParams.pInDepth = inDepth;
        D3D12DlssEvalParams.pInMotionVectors = inFlow;
        D3D12DlssEvalParams.pInExposureTexture = nullptr;
        D3D12DlssEvalParams.InJitterOffsetX = jitterOffX;
        D3D12DlssEvalParams.InJitterOffsetY = jitterOffY;
        D3D12DlssEvalParams.Feature.InSharpness = 0.0f;//flSharpness;
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
};

RT64::DLSS::DLSS(Device *device) {
	ctx = new Context(device);
}

RT64::DLSS::~DLSS() {
	delete ctx;
}

void RT64::DLSS::set(int inputWidth, int inputHeight, int outputWidth, int outputHeight) {
    ctx->set(inputWidth, inputHeight, outputWidth, outputHeight);
}

void RT64::DLSS::upscale(
    unsigned int inColorOffX, unsigned int inColorOffY,
    unsigned int inColorWidth, unsigned int inColorHeight,
    ID3D12Resource *inColor, ID3D12Resource *outColor, ID3D12Resource *inFlow, ID3D12Resource *inDepth,
    bool resetAccumulation, float jitterOffX, float jitterOffY)
{
	ctx->upscale(
        inColorOffX, inColorOffY,
        inColorWidth, inColorHeight,
        inColor, outColor, inFlow, inDepth,
        resetAccumulation, jitterOffX, jitterOffY);
}

/*
// Not everything is returned from GET_OPTIMAL_SETTINGS as it is unneeded for this sample.  However, everything is
// saved off for future use, since this sample should be updated as needed.
bool NGXWrapper::QueryOptimalSettings(uint2 inDisplaySize, NVSDK_NGX_PerfQuality_Value inQualValue, DlssRecommendedSettings *outRecommendedSettings)
{
    if (!IsNGXInitialized())
    {
        outRecommendedSettings->m_ngxRecommendedOptimalRenderSize = inDisplaySize;
        outRecommendedSettings->m_ngxDynamicMaximumRenderSize     = inDisplaySize;
        outRecommendedSettings->m_ngxDynamicMinimumRenderSize     = inDisplaySize;
        outRecommendedSettings->m_ngxRecommendedSharpness         = 0.0f;

        log::info("NGX was not initialized when querying Optimal Settings");
        return false;
    }

    NVSDK_NGX_Result Result = NGX_DLSS_GET_OPTIMAL_SETTINGS(m_ngxParameters,
        inDisplaySize.x, inDisplaySize.y, inQualValue,
        &outRecommendedSettings->m_ngxRecommendedOptimalRenderSize.x, &outRecommendedSettings->m_ngxRecommendedOptimalRenderSize.y,
        &outRecommendedSettings->m_ngxDynamicMaximumRenderSize    .x, &outRecommendedSettings->m_ngxDynamicMaximumRenderSize    .y,
        &outRecommendedSettings->m_ngxDynamicMinimumRenderSize    .x, &outRecommendedSettings->m_ngxDynamicMinimumRenderSize    .y,
        &outRecommendedSettings->m_ngxRecommendedSharpness);

    // Depending on what version of DLSS DLL is being used, a sharpness of > 1.0f was possible.
    // This makes sure we clamp to 1.0f.
    clamp(outRecommendedSettings->m_ngxRecommendedSharpness, 0.0f, 1.0f);

    if (NVSDK_NGX_FAILED(Result))
    {
        outRecommendedSettings->m_ngxRecommendedOptimalRenderSize   = inDisplaySize;
        outRecommendedSettings->m_ngxDynamicMaximumRenderSize       = inDisplaySize;
        outRecommendedSettings->m_ngxDynamicMinimumRenderSize       = inDisplaySize;
        outRecommendedSettings->m_ngxRecommendedSharpness           = 0.0f;

        log::warning("Querying Optimal Settings failed! code = 0x%08x, info: %ls", Result, GetNGXResultAsString(Result));
        return false;
    }

    return true;
}
*/