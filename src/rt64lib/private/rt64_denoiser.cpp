//
// RT64
//

#include <cuda_runtime.h>
#include <optix/optix.h>
#include <optix/optix_denoiser_tiling.h>
#include <optix/optix_function_table_definition.h>
#include <optix/optix_stubs.h>

#include "rt64_denoiser.h"
#include "rt64_device.h"

// Private

class RT64::Denoiser::Context {
private:
    struct Image {
        AllocatedResource resource;
        HANDLE sharedHandle;
        cudaExternalMemory_t extMemory;
        OptixImage2D optixImage;
    };

	Device *device = nullptr;
	OptixDeviceContext optixContext;
    Image color;
    Image albedo;
    Image normal;
    Image output;
    unsigned int width = 0;
    unsigned int height = 0;
	OptixDenoiser optixDenoiser = 0;
    CUdeviceptr optixIntensity = 0;
    CUdeviceptr optixAvgColor = 0;
    CUdeviceptr optixScratch = 0;
    uint32_t optixScratchSize = 0;
    CUdeviceptr optixState = 0;
    uint32_t optixStateSize = 0;
    UINT d3dNodeMask = 0;
    OptixDenoiserLayer optixDenoiserLayer;
    OptixDenoiserGuideLayer optixGuideLayer;
    OptixDenoiserParams optixParams;
public:
	Context(Device *device) {
		this->device = device;

        // Find node mask for this device.
        LUID deviceLuid = device->getD3D12Device()->GetAdapterLuid();
        cudaDeviceProp devProp;
        cudaGetDeviceProperties(&devProp, 0);
        if ((memcmp(&deviceLuid.LowPart, devProp.luid, sizeof(deviceLuid.LowPart)) == 0) &&
            (memcmp(&deviceLuid.HighPart, devProp.luid + sizeof(deviceLuid.LowPart), sizeof(deviceLuid.HighPart)) == 0))
        {
            d3dNodeMask = devProp.luidDeviceNodeMask;
        }

        cudaFree(nullptr);

        optixInit();
        OptixDeviceContextOptions ctxOptions = {};
        optixDeviceContextCreate(nullptr, &ctxOptions, &optixContext);

        OptixDenoiserOptions denOptions;
        denOptions.guideAlbedo = 1;
        denOptions.guideNormal = 1;

        OptixDenoiserModelKind modelKind = OPTIX_DENOISER_MODEL_KIND_LDR;
        optixDenoiserCreate(optixContext, modelKind, &denOptions, &optixDenoiser);
	}

	~Context() {
        releaseResources();
        optixDenoiserDestroy(optixDenoiser);
        optixDeviceContextDestroy(optixContext);
	}

    Image createImage(unsigned int width, unsigned int height) {
        Image image;
        image.resource = device->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, width * height * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, true);

        HRESULT result = device->getD3D12Device()->CreateSharedHandle(image.resource.Get(), NULL, GENERIC_ALL, NULL, &image.sharedHandle);
        D3D12_RESOURCE_DESC resDesc = image.resource.Get()->GetDesc();
        D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = device->getD3D12Device()->GetResourceAllocationInfo(d3dNodeMask, 1, &resDesc);
        cudaExternalMemoryHandleDesc extHandleDesc = {};
        extHandleDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
        extHandleDesc.handle.win32.handle = image.sharedHandle;
        extHandleDesc.size = resAllocInfo.SizeInBytes;
        extHandleDesc.flags = cudaExternalMemoryDedicated;
        cudaImportExternalMemory(&image.extMemory, &extHandleDesc);

        cudaExternalMemoryBufferDesc bufferDesc = {};
        bufferDesc.offset = 0;
        bufferDesc.size = resAllocInfo.SizeInBytes;
        bufferDesc.flags = 0;
        cudaExternalMemoryGetMappedBuffer(reinterpret_cast<void **>(&image.optixImage.data), image.extMemory, &bufferDesc);
        
        image.optixImage.width = width;
        image.optixImage.height = height;
        image.optixImage.rowStrideInBytes = width * sizeof(float4);
        image.optixImage.pixelStrideInBytes = sizeof(float4);
        image.optixImage.format = OPTIX_PIXEL_FORMAT_FLOAT4;

        return image;
    }

    void releaseImage(Image &image) {
        cudaFree(reinterpret_cast<void *>(image.optixImage.data));
        cudaDestroyExternalMemory(image.extMemory);
        CloseHandle(image.sharedHandle);
        image.resource.Release();
    }

    void releaseResources() {
        cudaFree(reinterpret_cast<void *>(optixIntensity));
        cudaFree(reinterpret_cast<void *>(optixScratch));
        cudaFree(reinterpret_cast<void *>(optixState));
        releaseImage(color);
        releaseImage(albedo);
        releaseImage(normal);
        releaseImage(output);
    }
    
    void denoise() {
        optixDenoiserComputeIntensity(optixDenoiser, nullptr, &color.optixImage, optixIntensity, optixScratch, optixScratchSize);
        optixUtilDenoiserInvokeTiled(optixDenoiser, nullptr, &optixParams, optixState, optixStateSize, &optixGuideLayer, &optixDenoiserLayer, 1, optixScratch, optixScratchSize, 0, width, height);
        cudaDeviceSynchronize();
    }

	void resize(unsigned int width, unsigned int height) {
        releaseResources();

        this->width = width;
        this->height = height;

        // Create new resources.
		color = createImage(width, height);
        albedo = createImage(width, height);
        normal = createImage(width, height);
        output = createImage(width, height);

        OptixDenoiserSizes denoiserSizes;
        optixDenoiserComputeMemoryResources(optixDenoiser, width, height, &denoiserSizes);
        optixScratchSize = static_cast<uint32_t>(denoiserSizes.withoutOverlapScratchSizeInBytes);
        optixStateSize = static_cast<uint32_t>(denoiserSizes.stateSizeInBytes);
        cudaMalloc(reinterpret_cast<void **>(&optixIntensity), sizeof(float));
        cudaMalloc(reinterpret_cast<void **>(&optixScratch), optixScratchSize);
        cudaMalloc(reinterpret_cast<void **>(&optixState), optixStateSize);
        optixDenoiserLayer.input = color.optixImage;
        optixDenoiserLayer.output = output.optixImage;
        optixGuideLayer.albedo = albedo.optixImage;
        optixGuideLayer.normal = normal.optixImage;
        optixDenoiserSetup(optixDenoiser, nullptr, width, height, optixState, optixStateSize, optixScratch, optixScratchSize);
        optixParams.denoiseAlpha = 0;
        optixParams.hdrIntensity = optixIntensity;
        optixParams.hdrAverageColor = 0;
        optixParams.blendFactor = 0.0f;
	}

	ID3D12Resource *getColor() const {
		return color.resource.Get();
	}

	ID3D12Resource *getAlbedo() const {
		return albedo.resource.Get();
	}

	ID3D12Resource *getNormal() const {
		return normal.resource.Get();
	}

	ID3D12Resource *getOutput() const {
		return output.resource.Get();
	}
};

RT64::Denoiser::Denoiser(Device *device) {
	ctx = new Context(device);
}

RT64::Denoiser::~Denoiser() {
	delete ctx;
}

void RT64::Denoiser::denoise() {
    ctx->denoise();
}

void RT64::Denoiser::resize(unsigned int width, unsigned int height) {
	ctx->resize(width, height);
}

ID3D12Resource *RT64::Denoiser::getColor() const {
	return ctx->getColor();
}

ID3D12Resource *RT64::Denoiser::getAlbedo() const {
	return ctx->getAlbedo();
}

ID3D12Resource *RT64::Denoiser::getNormal() const {
	return ctx->getNormal();
}

ID3D12Resource *RT64::Denoiser::getOutput() const {
	return ctx->getOutput();
}