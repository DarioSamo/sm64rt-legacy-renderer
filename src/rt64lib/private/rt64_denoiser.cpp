//
// RT64
//

#include "rt64_denoiser.h"
#include "rt64_device.h"

#include "optixDenoiser/OptiXDenoiser.h"

// Private

class RT64::Denoiser::Context {
private:
	Device *device;
    OptiXDenoiser denoiser;
    OptiXDenoiser::Data denoiserData;
	AllocatedResource inputColorRes;
	AllocatedResource inputAlbedoRes;
	AllocatedResource inputNormalRes;
	AllocatedResource outputDenoisedRes;
public:
	Context(Device *device) {
		this->device = device;
	}

	~Context() {
        denoiser.finish();
	}
    
    void denoise() {
        denoiser.exec();
    }

	AllocatedResource createSharedBuffer(unsigned int width, unsigned int height) {
		return device->allocateBuffer(D3D12_HEAP_TYPE_DEFAULT, width * height * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true, true);
	}

	void resize(unsigned int width, unsigned int height) {
		// TODO: Do not finish every time.
		denoiser.finish();

		inputColorRes.Release();
		inputAlbedoRes.Release();
		inputNormalRes.Release();
		outputDenoisedRes.Release();

		inputColorRes = createSharedBuffer(width, height);
		inputAlbedoRes = createSharedBuffer(width, height);
		inputNormalRes = createSharedBuffer(width, height);
		outputDenoisedRes = createSharedBuffer(width, height);

		denoiserData.width = width;
		denoiserData.height = height;
		denoiserData.color = inputColorRes.Get();
		denoiserData.albedo = inputAlbedoRes.Get();
		denoiserData.normal = inputNormalRes.Get();
		denoiserData.output = outputDenoisedRes.Get();

		// TODO: Do not init every time.
		denoiser.init(device->getD3D12Device(), denoiserData, 0, 0, false, false);
	}

	ID3D12Resource *getInputColorResource() const {
		return inputColorRes.Get();
	}

	ID3D12Resource *getInputAlbedoResource() const {
		return inputAlbedoRes.Get();
	}

	ID3D12Resource *getInputNormalResource() const {
		return inputNormalRes.Get();
	}

	ID3D12Resource *getOutputDenoisedResource() const {
		return outputDenoisedRes.Get();
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

ID3D12Resource *RT64::Denoiser::getInputColorResource() const {
	return ctx->getInputColorResource();
}

ID3D12Resource *RT64::Denoiser::getInputAlbedoResource() const {
	return ctx->getInputAlbedoResource();
}

ID3D12Resource *RT64::Denoiser::getInputNormalResource() const {
	return ctx->getInputNormalResource();
}

ID3D12Resource *RT64::Denoiser::getOutputDenoisedResource() const {
	return ctx->getOutputDenoisedResource();
}