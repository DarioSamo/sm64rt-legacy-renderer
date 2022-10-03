//
// RT64
//

#ifndef RT64_MINIMAL

#include "../public/rt64.h"

#include "rt64_texture.h"

#include "DDSTextureLoader/DDSTextureLoader12.h"

#include "rt64_device.h"
#include "rt64_mipmaps.h"

// Private

RT64::Texture::Texture(Device *device) {
	this->device = device;
	format = DXGI_FORMAT_UNKNOWN;
	currentIndex = -1;
}

RT64::Texture::~Texture() {
	texture.Release();
}

void RT64::Texture::setRawWithFormat(DXGI_FORMAT format, const void *bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps) {
	assert(bytes != nullptr);
	this->format = format;

	AllocatedResource textureUpload;
	Mipmaps *mipmaps = device->getMipmaps();
	if (mipmaps == nullptr) {
		generateMipmaps = false;
	}

	UINT16 mipLevels = generateMipmaps ? (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1) : 1;

	// Calculate the minimum row width required to store the texture.
	UINT rowWidth, rowPadding;
	CalculateTextureRowWidthPadding(rowPitch, rowWidth, rowPadding);

	{
		// Describe the texture
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.MipLevels = mipLevels;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Format = format;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		// Create the texture resource
		texture = device->allocateResource(D3D12_HEAP_TYPE_DEFAULT, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);

		// Describe the resource
		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Width = (rowWidth * height);
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;

		// Create the upload heap
		textureUpload = device->allocateResource(D3D12_HEAP_TYPE_UPLOAD, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);
	}

	// Upload texture.
	{
		// Copy the pixel data to the upload heap resource
		UINT8 *pData;
		textureUpload.Get()->Map(0, nullptr, reinterpret_cast<void **>(&pData));

		if (rowPadding == 0) {
			memcpy(pData, bytes, byteCount);
		}
		else {
			UINT8 *pSource = (UINT8 *)(bytes);
			size_t pOffset = 0;
			while ((pOffset + rowPitch) <= byteCount) {
				memcpy(pData, pSource, rowPitch);
				pSource += rowPitch;
				pOffset += rowPitch;
				pData += rowWidth;
			}
		}

		textureUpload.Get()->Unmap(0, nullptr);

		// Describe the upload heap resource location for the copy
		D3D12_SUBRESOURCE_FOOTPRINT subresource = {};
		subresource.Format = format;
		subresource.Width = width;
		subresource.Height = height;
		subresource.RowPitch = rowWidth;
		subresource.Depth = 1;

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		footprint.Offset = 0;
		footprint.Footprint = subresource;

		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = textureUpload.Get();
		source.PlacedFootprint = footprint;
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

		// Describe the default heap resource location for the copy
		D3D12_TEXTURE_COPY_LOCATION destination = {};
		destination.pResource = texture.Get();
		destination.SubresourceIndex = 0;
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		// Reset the command list.
		auto d3dCommandList = device->getD3D12CommandList();

		// Copy the buffer resource from the upload heap to the texture resource on the default heap.
		d3dCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

		// Transition the texture to a shader resource.
		CD3DX12_RESOURCE_BARRIER uploadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		d3dCommandList->ResourceBarrier(1, &uploadBarrier);
		d3dCommandList->DiscardResource(textureUpload.Get(), nullptr);
	}

	if (generateMipmaps) {
		mipmaps->generate(texture.Get());
	}
	else {
		device->submitCommandList();
		device->waitForGPU();
		device->resetCommandList();
	}

	textureUpload.Release();
}

void RT64::Texture::setRGBA8(const void *bytes, int byteCount, int width, int height, int rowPitch, bool generateMipmaps) {
	setRawWithFormat(DXGI_FORMAT_R8G8B8A8_UNORM, bytes, byteCount, width, height, rowPitch, generateMipmaps);
}

void RT64::Texture::setDDS(const void *bytes, int byteCount) {
	// Create the resource with the DDS data.
	std::vector<D3D12_SUBRESOURCE_DATA> subresourceData;
	D3D12MA::Allocation *textureAllocation = nullptr;
	D3D12_CHECK(LoadDDSTextureFromMemory(device->getD3D12Device(), (const uint8_t *)(bytes), byteCount, device->getD3D12Allocator(), &textureAllocation, subresourceData));

	texture = AllocatedResource(textureAllocation);
	D3D12_RESOURCE_DESC textureDesc = texture.Get()->GetDesc();
	format = textureDesc.Format;

	// Describe the resource.
	const UINT subresouceSize = static_cast<UINT>(subresourceData.size());
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, subresouceSize);
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Width = uploadBufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;

	// Create the upload heap.
	AllocatedResource textureUpload = device->allocateResource(D3D12_HEAP_TYPE_UPLOAD, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);

	// Update the subresources with the data from the DDS.
	auto d3dCommandList = device->getD3D12CommandList();
	UpdateSubresources(d3dCommandList, texture.Get(), textureUpload.Get(), 0, 0, subresouceSize, &subresourceData[0]);

	// Transition the texture to a shader resource.
	CD3DX12_RESOURCE_BARRIER uploadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	d3dCommandList->ResourceBarrier(1, &uploadBarrier);
	d3dCommandList->DiscardResource(textureUpload.Get(), nullptr);

	// Execute the commands before releasing the upload heap.
	device->submitCommandList();
	device->waitForGPU();
	device->resetCommandList();

	textureUpload.Release();
}

ID3D12Resource *RT64::Texture::getTexture() const {
	return texture.Get();
}

DXGI_FORMAT RT64::Texture::getFormat() const {
	return format;
}

void RT64::Texture::setCurrentIndex(int v) {
	currentIndex = v;
}

int RT64::Texture::getCurrentIndex() const {
	return currentIndex;
}

// Public

DLLEXPORT RT64_TEXTURE *RT64_CreateTexture(RT64_DEVICE *devicePtr, RT64_TEXTURE_DESC textureDesc) {
	assert(devicePtr != nullptr);
	RT64::Device *device = (RT64::Device *)(devicePtr);
	RT64::Texture *texture = new RT64::Texture(device);

	// Try to load the texture data.
	try {
		switch (textureDesc.format) {
		case RT64_TEXTURE_FORMAT_RGBA8:
			texture->setRGBA8(textureDesc.bytes, textureDesc.byteCount, textureDesc.width, textureDesc.height, textureDesc.rowPitch, true);
			break;
		case RT64_TEXTURE_FORMAT_DDS:
			texture->setDDS(textureDesc.bytes, textureDesc.byteCount);
			break;
		}

		return (RT64_TEXTURE *)(texture);
	}
	RT64_CATCH_EXCEPTION();

	delete texture;
	return nullptr;
}

DLLEXPORT void RT64_DestroyTexture(RT64_TEXTURE *texturePtr) {
	delete (RT64::Texture *)(texturePtr);
}

#endif