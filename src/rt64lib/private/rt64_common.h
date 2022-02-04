//
// RT64
//

#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <d3dcompiler.h>

#include <vector>

#include <dxgi1_4.h>
#include <d3d12.h>

#include "D3D12MemoryAllocator/D3D12MemAlloc.h"

#include <DirectXMath.h>

#include "../public/rt64.h"

#define DLLEXPORT extern "C" __declspec(dllexport)  

using namespace DirectX;

#define HEAP_INDEX(x) (int)(RT64::HeapIndices::x)
#define UAV_INDEX(x) (int)(RT64::UAVIndices::x)
#define SRV_INDEX(x) (int)(RT64::SRVIndices::x)
#define CBV_INDEX(x) (int)(RT64::CBVIndices::x)
#define SRV_TEXTURES_MAX 512

namespace RT64 {
	// Matches order in heap used in shader binding table.
	enum class HeapIndices : int {
		gViewDirection,
		gShadingPosition,
		gShadingNormal,
		gShadingSpecular,
		gDiffuse,
		gInstanceId,
		gDirectLightAccum,
		gIndirectLightAccum,
		gReflection,
		gRefraction,
		gTransparent,
		gVolumetricFog,
		gFlow,
		gNormal,
		gDepth,
		gPrevNormal,
		gPrevDepth,
		gPrevDirectLightAccum,
		gPrevIndirectLightAccum,
		gFilteredDirectLight,
		gFilteredIndirectLight,
		gFog,
		gSpecularLightAccum,
		gPrevSpecularLight,
		gHitDistAndFlow,
		gHitColor,
		gHitNormal,
		gHitSpecular,
		gHitInstanceId,
		gBackground,
		gParams,
		SceneBVH,
		SceneLights,
		instanceTransforms,
		instanceMaterials,
		gBlueNoise,
		gTextures,
		MAX
	};

	enum class UAVIndices : int {
		gViewDirection,
		gShadingPosition,
		gShadingNormal,
		gShadingSpecular,
		gDiffuse,
		gInstanceId,
		gDirectLightAccum,
		gIndirectLightAccum,
		gReflection,
		gRefraction,
		gTransparent,
		gVolumetricFog,
		gFlow,
		gNormal,
		gDepth,
		gPrevNormal,
		gPrevDepth,
		gPrevDirectLightAccum,
		gPrevIndirectLightAccum,
		gFilteredDirectLight,
		gFilteredIndirectLight,
		gFog,
		gSpecularLightAccum,
		gPrevSpecularLight,
		gHitDistAndFlow,
		gHitColor,
		gHitNormal,
		gHitSpecular,
		gHitInstanceId,
		MAX
	};

	enum class SRVIndices : int {
		SceneBVH,
		gBackground,
		vertexBuffer,
		indexBuffer,
		SceneLights,
		instanceTransforms,
		instanceMaterials,
		gBlueNoise,
		gTextures
	};

	enum class CBVIndices : int {
		gParams
	};

	enum class UpscaleMode {
		Bilinear,
		FSR,
#ifdef RT64_DLSS
		DLSS
#endif
	};

	// Some shared shader constants.
	static const unsigned int VisualizationModeFinal = 0;
	static const unsigned int VisualizationModeShadingPosition = 1;
	static const unsigned int VisualizationModeShadingNormal = 2;
	static const unsigned int VisualizationModeShadingSpecular = 3;
	static const unsigned int VisualizationModeDiffuse = 4;
	static const unsigned int VisualizationModeInstanceID = 5;
	static const unsigned int VisualizationModeDirectLightRaw = 6;
	static const unsigned int VisualizationModeDirectLightFiltered = 7;
	static const unsigned int VisualizationModeIndirectLightRaw = 8;
	static const unsigned int VisualizationModeIndirectLightFiltered = 9;
	static const unsigned int VisualizationModeReflection = 10;
	static const unsigned int VisualizationModeRefraction = 11;
	static const unsigned int VisualizationModeTransparent = 12;
	static const unsigned int VisualizationModeMotionVectors = 13;
	static const unsigned int VisualizationModeDepth = 14;

	// Error string for last error or exception that was caught.
	extern std::string GlobalLastError;

#ifdef NDEBUG
#	define RT64_LOG_OPEN(x)
#	define RT64_LOG_CLOSE()
#	define RT64_LOG_PRINTF(x, ...)
#else
	extern FILE *GlobalLogFile;
#	define RT64_LOG_OPEN(x) do { GlobalLogFile = fopen(x, "wt"); } while (0)
#	define RT64_LOG_CLOSE() do { fclose(GlobalLogFile); } while (0)
#	define RT64_LOG_PRINTF(x, ...) do { fprintf(GlobalLogFile, x, __VA_ARGS__); fprintf(GlobalLogFile, " (%s in %s:%d)\n", __FUNCTION__, __FILE__, __LINE__); fflush(GlobalLogFile); } while (0)
#endif



#ifndef RT64_MINIMAL
	class AllocatedResource {
	private:
		D3D12MA::Allocation *d3dMaAllocation;
	public:
		AllocatedResource() {
			d3dMaAllocation = nullptr;
		}

		AllocatedResource(D3D12MA::Allocation *d3dMaAllocation) {
			this->d3dMaAllocation = d3dMaAllocation;
		}

		~AllocatedResource() { }

		void SetName(LPCWSTR name) {
			if (!IsNull()) {
				Get()->SetName(name);
			}
		}

		inline ID3D12Resource *Get() const {
			if (!IsNull()) {
				return d3dMaAllocation->GetResource();
			}
			else {
				return nullptr;
			}
		}

		inline bool IsNull() const {
			return (d3dMaAllocation == nullptr);
		}

		void Release() {
			if (!IsNull()) {
				ID3D12Resource *d3dResource = d3dMaAllocation->GetResource();
				d3dMaAllocation->Release();
				d3dResource->Release();
				d3dMaAllocation = nullptr;
			}
		}
	};

	struct InstanceTransforms {
		XMMATRIX objectToWorld;
		XMMATRIX objectToWorldNormal;
		XMMATRIX objectToWorldPrevious;
	};

	struct AccelerationStructureBuffers {
		AllocatedResource scratch;
		UINT64 scratchSize;
		AllocatedResource result;
		UINT64 resultSize;
		AllocatedResource instanceDesc;
		UINT64 instanceDescSize;

		AccelerationStructureBuffers() {
			scratchSize = resultSize = instanceDescSize = 0;
		}

		void Release() {
			scratch.Release();
			result.Release();
			instanceDesc.Release();
			scratchSize = resultSize = instanceDescSize = 0;
		}
	};

	// Points to a static array of data in memory using the IDxcBlob interface.
	class StaticBlob : public IDxcBlob {
	private:
		const unsigned char *data;
		size_t dataSize;
		std::atomic<int> refCount;
	public:
		StaticBlob(const unsigned char *data, size_t dataSize) {
			this->data = data;
			this->dataSize = dataSize;
		}

		~StaticBlob() { }

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) override {
			// Always set out parameter to NULL, validating it first.
			if (!ppvObject) {
				return E_INVALIDARG;
			}

			*ppvObject = NULL;
			if (riid == IID_IUnknown) {
				// Increment the reference count and return the pointer.
				*ppvObject = (LPVOID)this;
				AddRef();
				return NOERROR;
			}

			return E_NOINTERFACE;
		}

		virtual ULONG STDMETHODCALLTYPE AddRef(void) override {
			refCount++;
			return refCount;
		}

		virtual ULONG STDMETHODCALLTYPE Release(void) override {
			refCount--;
			if (refCount == 0) {
				delete this;
			}

			return refCount;
		}

		virtual LPVOID STDMETHODCALLTYPE GetBufferPointer(void) override {
			return (LPVOID)(data);
		}

		virtual SIZE_T STDMETHODCALLTYPE GetBufferSize(void) override {
			return dataSize;
		}
	};

	inline void operator+=(RT64_VECTOR3 &a, const RT64_VECTOR3 &b) {
		a.x += b.x;
		a.y += b.y;
		a.z += b.z;
	}

	inline RT64_VECTOR3 operator+(const RT64_VECTOR3 &a, const RT64_VECTOR3 &b) {
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	inline RT64_VECTOR3 operator-(const RT64_VECTOR3 &a, const RT64_VECTOR3 &b) {
		return { a.x - b.x, a.y - b.y, a.z - b.z };
	}

	inline RT64_VECTOR3 operator*(const RT64_VECTOR3 &a, const float v) {
		return { a.x * v, a.y * v, a.z * v };
	}

	inline RT64_VECTOR3 operator/(const RT64_VECTOR3 &a, const float v) {
		return { a.x / v, a.y / v, a.z / v };
	}

	inline float Length(const RT64_VECTOR3 &a) {
		float sqrLength = a.x * a.x + a.y * a.y + a.z * a.z;
		return sqrt(sqrLength);
	}

	inline RT64_VECTOR3 Normalize(const RT64_VECTOR3 &a) {
		float l = Length(a);
		return (l > 0.0f) ? (a / l) : a;
	}

	inline RT64_VECTOR3 Cross(const RT64_VECTOR3 &a, const RT64_VECTOR3 &b) {
		return {
			a.y * b.z - b.y * a.z,
			a.z * b.x - b.z * a.x,
			a.x * b.y - b.x * a.y
		};
	}

	inline RT64_VECTOR3 DirectionFromTo(const RT64_VECTOR3 &a, const RT64_VECTOR3 &b) {
		RT64_VECTOR3 dir = { b.x - a.x,  b.y - a.y, b.z - a.z };
		float length = Length(dir);
		return dir / length;
	}

	inline RT64_VECTOR4 ToVector4(const RT64_VECTOR3& a, float w) {
		return { a.x, a.y, a.z, w };
	}

	inline void CalculateTextureRowWidthPadding(UINT rowPitch, UINT &rowWidth, UINT &rowPadding) {
		const int RowMultiple = 256;
		rowWidth = rowPitch;
		rowPadding = (rowWidth % RowMultiple) ? RowMultiple - (rowWidth % RowMultiple) : 0;
		rowWidth += rowPadding;
	}

	inline float HaltonSequence(int i, int b) {
		float f = 1.0;
		float r = 0.0;
		while (i > 0) {
			f = f / float(b);
			r = r + f * float(i % b);
			i = i / b;
		}

		return r;
	}

	inline RT64_VECTOR2 HaltonJitter(int frame, int phases) {
		return { HaltonSequence(frame % phases + 1, 2) - 0.5f, HaltonSequence(frame % phases + 1, 3) - 0.5f };
	}
#endif
};

#define D3D12_CHECK( call )                                                         \
    do                                                                              \
    {                                                                               \
        HRESULT hr = call;                                                          \
        if (FAILED(hr))														        \
        {																	        \
			char errorMessage[512];													\
			snprintf(errorMessage, sizeof(errorMessage), "D3D12 call " #call " "	\
				"failed with error code %X.", hr);									\
																					\
            throw std::runtime_error(errorMessage);                                 \
        }                                                                           \
    } while( 0 )

#define RT64_CATCH_EXCEPTION()							\
	catch (const std::runtime_error &e) {				\
		RT64::GlobalLastError = std::string(e.what());	\
		fprintf(stderr, "%s\n", e.what());				\
	}

#ifndef RT64_MINIMAL
namespace nv_helpers_dx12
{
#ifndef ROUND_UP
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))
#endif

	ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible);
}
#endif