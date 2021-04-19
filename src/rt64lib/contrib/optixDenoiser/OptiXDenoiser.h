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


#pragma once

#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#include <optix_denoiser_tiling.h>

#include "OptiXError.h"

#include <cuda_runtime.h>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iomanip>
#include <vector>

static void context_log_cb( uint32_t level, const char* tag, const char* message, void* /*cbdata*/ )
{
    if( level < 4 )
        std::cerr << "[" << std::setw( 2 ) << level << "][" << std::setw( 12 ) << tag << "]: "
                  << message << "\n";
}

// create four channel float OptixImage2D with given dimension. allocate memory on device and
// copy data from host memory given in hmem to device if hmem is nonzero.
static OptixImage2D createOptixImage2D( ID3D12Device *device, unsigned int width, unsigned int height, ID3D12Resource *extRes = 0 )
{
    OptixImage2D oi;
    if(extRes != 0) {
        LUID deviceLuid = device->GetAdapterLuid();
        cudaDeviceProp devProp;
        cudaGetDeviceProperties(&devProp, 0);
        UINT nodeMask = 0;
        if ((memcmp(&deviceLuid.LowPart, devProp.luid, sizeof(deviceLuid.LowPart)) == 0) &&
            (memcmp(&deviceLuid.HighPart, devProp.luid + sizeof(deviceLuid.LowPart), sizeof(deviceLuid.HighPart)) == 0))
        {
            nodeMask = devProp.luidDeviceNodeMask;
        }

        HANDLE sharedHandle = 0;
        HRESULT result = device->CreateSharedHandle(extRes, NULL, GENERIC_ALL, NULL, &sharedHandle);
        assert(result == S_OK);
        
        D3D12_RESOURCE_DESC resDesc = extRes->GetDesc();
        D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = device->GetResourceAllocationInfo(nodeMask, 1, &resDesc);
        cudaExternalMemoryHandleDesc extHandleDesc = {};
        extHandleDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
        extHandleDesc.handle.win32.handle = sharedHandle;
        extHandleDesc.size = resAllocInfo.SizeInBytes;
        extHandleDesc.flags = cudaExternalMemoryDedicated;

        cudaExternalMemory_t extMemory;
        CUDA_CHECK(cudaImportExternalMemory(&extMemory, &extHandleDesc));

        cudaExternalMemoryBufferDesc bufferDesc = {};
        bufferDesc.offset = 0;
        bufferDesc.size = resAllocInfo.SizeInBytes;
        bufferDesc.flags = 0;
        CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(reinterpret_cast<void **>(&oi.data), extMemory, &bufferDesc));
    }
    else {
        const uint64_t frame_byte_size = width * height * sizeof(float4);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&oi.data), frame_byte_size));
    }

    oi.width              = width;
    oi.height             = height;
    oi.rowStrideInBytes   = width*sizeof(float4);
    oi.pixelStrideInBytes = sizeof(float4);
    oi.format             = OPTIX_PIXEL_FORMAT_FLOAT4;
    return oi;
}

class OptiXDenoiser
{
public:
    struct Data
    {
        uint32_t width = 0;
        uint32_t height = 0;
        ID3D12Resource *color = 0;
        ID3D12Resource *albedo = 0;
        ID3D12Resource *normal = 0;
        ID3D12Resource *flow = 0;
        ID3D12Resource *output = 0;
        std::vector<ID3D12Resource *> aovs; // input AOVs
    };

    // Initialize the API and push all data to the GPU -- normaly done only once per session
    // tileWidth, tileHeight: if nonzero, enable tiling with given dimension
    // kpMode: if enabled, use kernel prediction model even if no AOVs are given
    // temporalMode: if enabled, use a model for denoising sequences of images
    void init( ID3D12Device *device,
               const Data&  data,
               unsigned int tileWidth = 0,
               unsigned int tileHeight = 0,
               bool         kpMode = false,
               bool         temporalMode = false );

    // Execute the denoiser. In interactive sessions, this would be done once per frame/subframe
    void exec();

    // Cleanup state, deallocate memory -- normally done only once per render session
    void finish(); 
private:
    OptixDeviceContext    m_context      = nullptr;
    OptixDenoiser         m_denoiser     = nullptr;
    OptixDenoiserParams   m_params       = {};

    bool                  m_temporalMode;

    CUdeviceptr           m_intensity    = 0;
    CUdeviceptr           m_avgColor     = 0;
    CUdeviceptr           m_scratch      = 0;
    uint32_t              m_scratch_size = 0;
    CUdeviceptr           m_state        = 0;
    uint32_t              m_state_size   = 0;

    unsigned int          m_tileWidth    = 0;
    unsigned int          m_tileHeight   = 0;
    unsigned int          m_overlap      = 0;

    OptixDenoiserGuideLayer           m_guideLayer = {};
    std::vector< OptixDenoiserLayer > m_layers;
};

void OptiXDenoiser::init( ID3D12Device *device, 
                          const Data&  data,
                          unsigned int tileWidth,
                          unsigned int tileHeight,
                          bool         kpMode,
                          bool         temporalMode )
{
    SUTIL_ASSERT( data.color  );
    SUTIL_ASSERT( data.width  );
    SUTIL_ASSERT( data.height );
    SUTIL_ASSERT_MSG( !data.normal || data.albedo, "Currently albedo is required if normal input is given" );
    SUTIL_ASSERT_MSG( ( tileWidth == 0 && tileHeight == 0 ) || ( tileWidth > 0 && tileHeight > 0 ), "tile size must be > 0 for width and height" );

    m_temporalMode = temporalMode;

    m_tileWidth  = tileWidth > 0 ? tileWidth : data.width;
    m_tileHeight = tileHeight > 0 ? tileHeight : data.height;

    //
    // Initialize CUDA and create OptiX context
    //
    {
        // Initialize CUDA
        CUDA_CHECK( cudaFree( nullptr ) );

        CUcontext cu_ctx = nullptr;  // zero means take the current context
        OPTIX_CHECK( optixInit() );
        OptixDeviceContextOptions options = {};
        options.logCallbackFunction       = &context_log_cb;
        options.logCallbackLevel          = 4;
        OPTIX_CHECK( optixDeviceContextCreate( cu_ctx, &options, &m_context ) );
    }

    //
    // Create denoiser
    //
    {
        /*****
        // load user provided model if model.bin is present in the currrent directory,
        // configuration of filename not done here.
        std::ifstream file( "model.bin" );
        if ( file.good() ) {
            std::stringstream source_buffer;
            source_buffer << file.rdbuf();
            OPTIX_CHECK( optixDenoiserCreateWithUserModel( m_context, (void*)source_buffer.str().c_str(), source_buffer.str().size(), &m_denoiser ) );
        }
        else
        *****/
        {
            OptixDenoiserOptions options = {};
            options.guideAlbedo = data.albedo ? 1 : 0;
            options.guideNormal = data.normal ? 1 : 0;

            OptixDenoiserModelKind modelKind;
            if( kpMode || data.aovs.size() > 0 )
            {
                SUTIL_ASSERT( !temporalMode );
                modelKind = OPTIX_DENOISER_MODEL_KIND_AOV;
            }
            else
            {
                modelKind = temporalMode ? OPTIX_DENOISER_MODEL_KIND_TEMPORAL : /*OPTIX_DENOISER_MODEL_KIND_HDR*/OPTIX_DENOISER_MODEL_KIND_LDR;
            }
            OPTIX_CHECK( optixDenoiserCreate( m_context, modelKind, &options, &m_denoiser ) );
        }
    }


    //
    // Allocate device memory for denoiser
    //
    {
        OptixDenoiserSizes denoiser_sizes;

        OPTIX_CHECK( optixDenoiserComputeMemoryResources(
                    m_denoiser,
                    m_tileWidth,
                    m_tileHeight,
                    &denoiser_sizes
                    ) );

        if( tileWidth == 0 )
        {
            m_scratch_size = static_cast<uint32_t>( denoiser_sizes.withoutOverlapScratchSizeInBytes );
            m_overlap = 0;
        }
        else
        {
            m_scratch_size = static_cast<uint32_t>( denoiser_sizes.withOverlapScratchSizeInBytes );
            m_overlap = denoiser_sizes.overlapWindowSizeInPixels;
        }

        if( data.aovs.size() == 0 && kpMode == false )
        {
            CUDA_CHECK( cudaMalloc(
                        reinterpret_cast<void**>( &m_intensity ),
                        sizeof( float )
                        ) );
        }
        else
        {
            CUDA_CHECK( cudaMalloc(
                        reinterpret_cast<void**>( &m_avgColor ),
                        3 * sizeof( float )
                        ) );
        }

        CUDA_CHECK( cudaMalloc(
                    reinterpret_cast<void**>( &m_scratch ),
                    m_scratch_size 
                    ) );

        CUDA_CHECK( cudaMalloc(
                    reinterpret_cast<void**>( &m_state ),
                    denoiser_sizes.stateSizeInBytes
                    ) );

        m_state_size = static_cast<uint32_t>( denoiser_sizes.stateSizeInBytes );

        OptixDenoiserLayer layer = {};
        layer.input  = createOptixImage2D( device, data.width, data.height, data.color );
        layer.output = createOptixImage2D( device, data.width, data.height, data.output );
        if( m_temporalMode )
        {
            // this is the first frame, create zero motion vector image
            void * flowmem;
            CUDA_CHECK( cudaMalloc( &flowmem, data.width * data.height * sizeof( float4 ) ) );
            CUDA_CHECK( cudaMemset( flowmem, 0, data.width * data.height * sizeof(float4) ) );
            m_guideLayer.flow = {(CUdeviceptr)flowmem, data.width, data.height, (unsigned int)(data.width * sizeof( float4 )), (unsigned int)sizeof( float4 ), OPTIX_PIXEL_FORMAT_FLOAT4 };

            layer.previousOutput = layer.input;         // first frame
        }
        m_layers.push_back( layer );

        if( data.albedo )
            m_guideLayer.albedo = createOptixImage2D( device, data.width, data.height, data.albedo );
        if( data.normal )
            m_guideLayer.normal = createOptixImage2D( device, data.width, data.height, data.normal );
    }

    //
    // Setup denoiser
    //
    {
        OPTIX_CHECK( optixDenoiserSetup(
                    m_denoiser,
                    nullptr,  // CUDA stream
                    m_tileWidth + 2 * m_overlap,
                    m_tileHeight + 2 * m_overlap,
                    m_state,
                    m_state_size,
                    m_scratch,
                    m_scratch_size
                    ) );


        m_params.denoiseAlpha    = 0;
        m_params.hdrIntensity    = m_intensity;
        m_params.hdrAverageColor = m_avgColor;
        m_params.blendFactor     = 0.0f;
    }
}

void OptiXDenoiser::exec()
{
    if( m_intensity )
    {
        OPTIX_CHECK( optixDenoiserComputeIntensity(
                    m_denoiser,
                    nullptr, // CUDA stream
                    &m_layers[0].input,
                    m_intensity,
                    m_scratch,
                    m_scratch_size
                    ) );
    }
    
    if( m_avgColor )
    {
        OPTIX_CHECK( optixDenoiserComputeAverageColor(
                    m_denoiser,
                    nullptr, // CUDA stream
                    &m_layers[0].input,
                    m_avgColor,
                    m_scratch,
                    m_scratch_size
                    ) );
    }

    OPTIX_CHECK( optixUtilDenoiserInvokeTiled(
                m_denoiser,
                nullptr, // CUDA stream
                &m_params,
                m_state,
                m_state_size,
                &m_guideLayer,
                m_layers.data(),
                static_cast<unsigned int>( m_layers.size() ),
                m_scratch,
                m_scratch_size,
                m_overlap,
                m_tileWidth,
                m_tileHeight
                ) );

    CUDA_SYNC_CHECK();
}

inline float catmull_rom(
    float       p[4],
    float       t)
{
    return p[1] + 0.5f * t * ( p[2] - p[0] + t * ( 2.f * p[0] - 5.f * p[1] + 4.f * p[2] - p[3] + t * ( 3.f * ( p[1] - p[2]) + p[3] - p[0] ) ) );
}

// apply flow to image at given pixel position (using bilinear interpolation), write back RGB result.
static void addFlow(
    float4*             result,
    const float4*       image,
    const float4*       flow,
    unsigned int        width,
    unsigned int        height,
    unsigned int        x,
    unsigned int        y )
{
    float dst_x = float( x ) - flow[x + y * width].x;
    float dst_y = float( y ) - flow[x + y * width].y;

    float x0 = dst_x - 1.f;
    float y0 = dst_y - 1.f;

    float r[4][4], g[4][4], b[4][4];
    for (int j=0; j < 4; j++)
    {
        for (int k=0; k < 4; k++)
        {
            int tx = static_cast<int>( x0 ) + k;
            if( tx < 0 )
                tx = 0;
            else if( tx >= (int)width )
                tx = width - 1;

            int ty = static_cast<int>( y0 ) + j;
            if( ty < 0 )
                ty = 0;
            else if( ty >= (int)height )
                ty = height - 1;

            r[j][k] = image[tx + ty * width].x;
            g[j][k] = image[tx + ty * width].y;
            b[j][k] = image[tx + ty * width].z;
        }
    }
    float tx = dst_x <= 0.f ? 0.f : dst_x - floorf( dst_x );

    r[0][0] = catmull_rom( r[0], tx );
    r[0][1] = catmull_rom( r[1], tx );
    r[0][2] = catmull_rom( r[2], tx );
    r[0][3] = catmull_rom( r[3], tx );

    g[0][0] = catmull_rom( g[0], tx );
    g[0][1] = catmull_rom( g[1], tx );
    g[0][2] = catmull_rom( g[2], tx );
    g[0][3] = catmull_rom( g[3], tx );

    b[0][0] = catmull_rom( b[0], tx );
    b[0][1] = catmull_rom( b[1], tx );
    b[0][2] = catmull_rom( b[2], tx );
    b[0][3] = catmull_rom( b[3], tx );

    float ty = dst_y <= 0.f ? 0.f : dst_y - floorf( dst_y );

    result[y * width + x].x = catmull_rom( r[0], ty );
    result[y * width + x].y = catmull_rom( g[0], ty );
    result[y * width + x].z = catmull_rom( b[0], ty );
}

void OptiXDenoiser::finish() 
{
    if (m_denoiser) {
        // Cleanup resources
        optixDenoiserDestroy(m_denoiser);
        optixDeviceContextDestroy(m_context);

        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_intensity)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_avgColor)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_scratch)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_state)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_guideLayer.albedo.data)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_guideLayer.normal.data)));
        CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_guideLayer.flow.data)));
        for (size_t i = 0; i < m_layers.size(); i++)
            CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_layers[i].input.data)));
        for (size_t i = 0; i < m_layers.size(); i++)
            CUDA_CHECK(cudaFree(reinterpret_cast<void *>(m_layers[i].output.data)));

        m_layers.clear();
    }
}
