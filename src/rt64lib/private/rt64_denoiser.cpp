//
// RT64
//

#include <cuda_runtime.h>
#include <optix/optix.h>
#include <optix/optix_denoiser_tiling.h>
#include <optix/optix_function_table_definition.h>
#include <optix/optix_stubs.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "rt64_denoiser.h"
#include "rt64_device.h"

// Implementation of NVIDIA's OptiX denoiser for real-time use with D3D12.
// Mostly based on the code from the denoiser example included in the OptiX SDK.
//
// Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

// Error checking macros.

#define OPTIX_CHECK( call )                                                    \
    do                                                                         \
    {                                                                          \
        OptixResult error = call;                                              \
        if( error != OPTIX_SUCCESS )                                           \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "OptiX call (" << #call << " ) failed with error: '"         \
               << optixGetErrorString(error)                                   \
               << "' (" __FILE__ << ":" << __LINE__ << ")\n";                  \
            throw std::runtime_error(ss.str());                                \
        }                                                                      \
    } while( 0 )

#define CUDA_CHECK( call )                                                     \
    do                                                                         \
    {                                                                          \
        cudaError_t error = call;                                              \
        if( error != cudaSuccess )                                             \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "CUDA call (" << #call << " ) failed with error: '"          \
               << cudaGetErrorString(error)                                    \
               << "' (" __FILE__ << ":" << __LINE__ << ")\n";                  \
            throw std::runtime_error(ss.str());                                \
        }                                                                      \
    } while( 0 )

// Private

class RT64::Denoiser::Context {
private:
    struct Image {
        ID3D12Resource *resource = nullptr;
        HANDLE sharedHandle = 0;
        cudaMipmappedArray_t mipmappedArray = 0;
        cudaArray_t extArray = 0;
        cudaExternalMemory_t extMemory = 0;
        OptixImage2D optixImage = { 0, 0, 0, 0, 0, OPTIX_PIXEL_FORMAT_FLOAT4 };
    };

    Image color;
    Image albedo;
    Image normal;
    Image output;
	Device *device = nullptr;
	OptixDeviceContext optixContext = 0;
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
    bool initialized;
public:
	Context(Device *device) {
		this->device = device;

        try {
            CUDA_CHECK(cudaFree(nullptr));

            OPTIX_CHECK(optixInit());
            OptixDeviceContextOptions ctxOptions = {};
            OPTIX_CHECK(optixDeviceContextCreate(nullptr, &ctxOptions, &optixContext));

            OptixDenoiserOptions denOptions;
            denOptions.guideAlbedo = 1;
            denOptions.guideNormal = 1;

            OptixDenoiserModelKind modelKind = OPTIX_DENOISER_MODEL_KIND_LDR;
            OPTIX_CHECK(optixDenoiserCreate(optixContext, modelKind, &denOptions, &optixDenoiser));

            // Find node mask for this device.
            LUID deviceLuid = device->getD3D12Device()->GetAdapterLuid();
            cudaDeviceProp devProp;
            CUDA_CHECK(cudaGetDeviceProperties(&devProp, 0));
            if ((memcmp(&deviceLuid.LowPart, devProp.luid, sizeof(deviceLuid.LowPart)) == 0) &&
                (memcmp(&deviceLuid.HighPart, devProp.luid + sizeof(deviceLuid.LowPart), sizeof(deviceLuid.HighPart)) == 0))
            {
                d3dNodeMask = devProp.luidDeviceNodeMask;
            }

            initialized = true;
        }
        catch (const std::runtime_error &e) {
            fprintf(stderr, "Denoiser could not be initialized: %s\n", e.what());
            initialized = false;
        }
	}

	~Context() {
        releaseResources();
        optixDenoiserDestroy(optixDenoiser);
        optixDeviceContextDestroy(optixContext);
	}

    Image createFromResource(unsigned int width, unsigned int height, ID3D12Resource *resource) {
        Image image;
        image.resource = resource;

        // Create shared handle for texture resource.
        D3D12_RESOURCE_DESC resDesc = resource->GetDesc();
        HRESULT result = device->getD3D12Device()->CreateSharedHandle(resource, NULL, GENERIC_ALL, NULL, &image.sharedHandle);
        D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = device->getD3D12Device()->GetResourceAllocationInfo(d3dNodeMask, 1, &resDesc);

        // Import shared handle as external memory for CUDA.
        cudaExternalMemoryHandleDesc extHandleDesc = {};
        extHandleDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
        extHandleDesc.handle.win32.handle = image.sharedHandle;
        extHandleDesc.size = resAllocInfo.SizeInBytes;
        extHandleDesc.flags = cudaExternalMemoryDedicated;
        CUDA_CHECK(cudaImportExternalMemory(&image.extMemory, &extHandleDesc));

        // Get the mipmapped array from the external memory.
        cudaExternalMemoryMipmappedArrayDesc mipmappedArrayDesc;
        mipmappedArrayDesc.offset = 0;
        mipmappedArrayDesc.numLevels = 1;
        mipmappedArrayDesc.formatDesc = cudaCreateChannelDesc(32, 32, 32, 32, cudaChannelFormatKindFloat);
        mipmappedArrayDesc.extent.width = width;
        mipmappedArrayDesc.extent.height = height;
        mipmappedArrayDesc.extent.depth = 0;
        mipmappedArrayDesc.flags = cudaArrayColorAttachment;
        CUDA_CHECK(cudaExternalMemoryGetMappedMipmappedArray(&image.mipmappedArray, image.extMemory, &mipmappedArrayDesc));

        // Get the first level as a CUDA array.
        CUDA_CHECK(cudaGetMipmappedArrayLevel(&image.extArray, image.mipmappedArray, 0));

        // Allocate the dedicated memory packed for the denoiser.
        image.optixImage.width = width;
        image.optixImage.height = height;
        image.optixImage.rowStrideInBytes = width * sizeof(float4);
        image.optixImage.pixelStrideInBytes = sizeof(float4);
        image.optixImage.format = OPTIX_PIXEL_FORMAT_FLOAT4;
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&image.optixImage.data), width * height * sizeof(float4)));

        return image;
    }

    void copyImageResToCuda(Image &image) {
        CUDA_CHECK(cudaMemcpy2DFromArray(reinterpret_cast<void *>(image.optixImage.data), image.optixImage.rowStrideInBytes, image.extArray, 0, 0, image.optixImage.rowStrideInBytes, image.optixImage.height, cudaMemcpyDeviceToDevice));
    }

    void copyImageCudaToRes(Image &image) {
        CUDA_CHECK(cudaMemcpy2DToArray(image.extArray, 0, 0, reinterpret_cast<void *>(image.optixImage.data), image.optixImage.rowStrideInBytes, image.optixImage.rowStrideInBytes, image.optixImage.height, cudaMemcpyDeviceToDevice));
    }

    void releaseImage(Image &image) {
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(image.optixImage.data)));

        if (image.mipmappedArray) {
            CUDA_CHECK(cudaFreeMipmappedArray(image.mipmappedArray));
        }

        if (image.extMemory) {
            CUDA_CHECK(cudaDestroyExternalMemory(image.extMemory));
        }

        if (image.sharedHandle) {
            CloseHandle(image.sharedHandle);
        }
    }

    void releaseResources() {
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(optixIntensity)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(optixScratch)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(optixState)));
        releaseImage(color);
        releaseImage(albedo);
        releaseImage(normal);
        releaseImage(output);
    }
    
    void denoise() {
        if (initialized) {
            copyImageResToCuda(color);
            copyImageResToCuda(albedo);
            copyImageResToCuda(normal);
            OPTIX_CHECK(optixDenoiserComputeIntensity(optixDenoiser, nullptr, &color.optixImage, optixIntensity, optixScratch, optixScratchSize));
            OPTIX_CHECK(optixUtilDenoiserInvokeTiled(optixDenoiser, nullptr, &optixParams, optixState, optixStateSize, &optixGuideLayer, &optixDenoiserLayer, 1, optixScratch, optixScratchSize, 0, width, height));
            copyImageCudaToRes(output);
            CUDA_CHECK(cudaDeviceSynchronize());
        }
    }

    void set(unsigned int width, unsigned int height, ID3D12Resource *inOutColor, ID3D12Resource *inAlbedo, ID3D12Resource *inNormal) {
        if (initialized) {
            releaseResources();

            this->width = width;
            this->height = height;

            // Create the Optix images with references to the resources.
            // Since Optix allocates its own CUDA memory, it is safe to create more
            // than one reference to the same resource.
            color = createFromResource(width, height, inOutColor);
            albedo = createFromResource(width, height, inAlbedo);
            normal = createFromResource(width, height, inNormal);
            output = createFromResource(width, height, inOutColor);

            // Setup Optix denoiser.
            OptixDenoiserSizes denoiserSizes;
            OPTIX_CHECK(optixDenoiserComputeMemoryResources(optixDenoiser, width, height, &denoiserSizes));
            optixScratchSize = static_cast<uint32_t>(denoiserSizes.withoutOverlapScratchSizeInBytes);
            optixStateSize = static_cast<uint32_t>(denoiserSizes.stateSizeInBytes);
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&optixIntensity), sizeof(float)));
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&optixScratch), optixScratchSize));
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&optixState), optixStateSize));
            optixDenoiserLayer.input = color.optixImage;
            optixDenoiserLayer.output = output.optixImage;
            optixGuideLayer.albedo = albedo.optixImage;
            optixGuideLayer.normal = normal.optixImage;
            OPTIX_CHECK(optixDenoiserSetup(optixDenoiser, nullptr, width, height, optixState, optixStateSize, optixScratch, optixScratchSize));
            optixParams.denoiseAlpha = 0;
            optixParams.hdrIntensity = optixIntensity;
            optixParams.hdrAverageColor = 0;
            optixParams.blendFactor = 0.0f;
        }
    }
};

#ifdef DENOISER_THREAD_CRASH_WORKAROUND

class RT64::Denoiser::WorkerThread {
public:
    struct Task {
        enum Action {
            Set,
            Denoise,
            Destroy
        };

        Action a;
        unsigned int width;
        unsigned int height;
        ID3D12Resource *inOutColor;
        ID3D12Resource *inAlbedo;
        ID3D12Resource *inNormal;
    };

    Context *ctx;
    Device *device;
    std::thread *thread;
    std::mutex queueMutex;
    std::mutex finishedMutex;
    std::condition_variable queueCondition;
    std::condition_variable finishedCondition;
    std::queue<Task> tasks;

    WorkerThread(Device *device) {
        this->device = device;
        ctx = nullptr;
        thread = new std::thread(&WorkerThread::run, this);
    }

    ~WorkerThread() {
        thread->join();
        delete thread;
    }

    void run() {
        std::unique_lock<std::mutex> lock(queueMutex);
        ctx = new Context(device);

        bool running = true;
        while (running) {
            if (!tasks.empty()) {
                Task t = tasks.front();
                tasks.pop();

                switch (t.a) {
                case Task::Set: {
                    ctx->set(t.width, t.height, t.inOutColor, t.inAlbedo, t.inNormal);
                    break;
                }
                case Task::Denoise: {
                    ctx->denoise();
                    break;
                }
                case Task::Destroy:
                    running = false;
                    break;
                }
            }
            else {
                {
                    std::unique_lock<std::mutex> lock(finishedMutex);
                    finishedCondition.notify_all();
                }

                queueCondition.wait(lock);
            }
        }

        delete ctx;
    }

    void doTaskBlocking(const Task &t) {
        std::unique_lock<std::mutex> finishLock(finishedMutex);
        {
            std::unique_lock<std::mutex> queueLock(queueMutex);
            tasks.push(t);
        }

        queueCondition.notify_all();
        finishedCondition.wait(finishLock);
    }
};

RT64::Denoiser::Denoiser(Device *device) {
    thread = new WorkerThread(device);
}

RT64::Denoiser::~Denoiser() {
    WorkerThread::Task t;
    t.a = WorkerThread::Task::Destroy;
    thread->doTaskBlocking(t);
    delete thread;
}

void RT64::Denoiser::denoise() {
    WorkerThread::Task t;
    t.a = WorkerThread::Task::Denoise;
    thread->doTaskBlocking(t);
}

void RT64::Denoiser::set(unsigned int width, unsigned int height, ID3D12Resource *inOutColor, ID3D12Resource *inAlbedo, ID3D12Resource *inNormal) {
    WorkerThread::Task t;
    t.a = WorkerThread::Task::Set;
    t.width = width;
    t.height = height;
    t.inOutColor = inOutColor;
    t.inAlbedo = inAlbedo;
    t.inNormal = inNormal;
    thread->doTaskBlocking(t);
}

#else

RT64::Denoiser::Denoiser(Device *device) {
    ctx = new Context(device);
}

RT64::Denoiser::~Denoiser() {
    delete ctx;
}

void RT64::Denoiser::denoise() {
    ctx->denoise();
}

void RT64::Denoiser::set(unsigned int width, unsigned int height, ID3D12Resource *inOutColor, ID3D12Resource *inAlbedo, ID3D12Resource *inNormal) {
    ctx->set(width, height, inOutColor, inAlbedo, inNormal);
}

#endif