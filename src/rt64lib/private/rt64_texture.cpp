//
// RT64
//

#include "../public/rt64.h"

#include "rt64_texture.h"

#include "rt64_device.h"

// Private

RT64::Texture::Texture(Device *device, const void *bytes, int width, int height, int stride) {
	assert(bytes != nullptr);

	this->device = device;

	// Create texture.
	const int RowMultiple = 256;
	UINT rowWidth = width * stride;
	UINT rowPadding = (rowWidth % RowMultiple) ? RowMultiple - (rowWidth % RowMultiple) : 0;
	rowWidth += rowPadding;

	{
		// Describe the texture
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.MipLevels = 1;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
		UINT8* pData;
		textureUpload.Get()->Map(0, nullptr, reinterpret_cast<void**>(&pData));

		if (rowPadding == 0) {
			memcpy(pData, bytes, width * height * stride);
		}
		else {
			for (int row = 0; row < height; row++) {
				memcpy(pData, (unsigned char *)(bytes) + row * width * stride, width * stride);
				pData += rowWidth;
			}
		}

		textureUpload.Get()->Unmap(0, nullptr);

		// Describe the upload heap resource location for the copy
		D3D12_SUBRESOURCE_FOOTPRINT subresource = {};
		subresource.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		subresource.Width = width;
		subresource.Height = height;
		subresource.RowPitch = rowWidth;
		subresource.Depth = 1;

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		footprint.Offset = 0;
		//footprint.Offset = texture.offset;
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
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		device->setLastCopyQueueBarrier(barrier);
	}
}

RT64::Texture::~Texture() {
	texture.Release();
	textureUpload.Release();
}

ID3D12Resource *RT64::Texture::getTexture() {
	return texture.Get();
}

// Public

DLLEXPORT RT64_TEXTURE *RT64_CreateTextureFromRGBA8(RT64_DEVICE *devicePtr, const void *bytes, int width, int height, int stride) {
	RT64::Device *device = (RT64::Device *)(devicePtr);
	return (RT64_TEXTURE *)(new RT64::Texture(device, bytes, width, height, stride));
}

DLLEXPORT void RT64_DestroyTexture(RT64_TEXTURE *texturePtr) {
	delete (RT64::Texture *)(texturePtr);
}