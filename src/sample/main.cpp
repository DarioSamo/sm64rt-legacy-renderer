//
// RT64 SAMPLE
//

#ifndef NDEBUG
#	define RT64_DEBUG
#endif

#include "rt64.h"

#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <Windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

struct {
	RT64_LIBRARY lib;
	RT64_LIGHT lights[16];
	int lightCount;
	RT64_DEVICE *device = nullptr;
	RT64_INSPECTOR* inspector = nullptr;
	RT64_SCENE *scene = nullptr;
	RT64_VIEW *view = nullptr;
	RT64_MESH *mesh = nullptr;
	RT64_TEXTURE *textureDif = nullptr;
	RT64_TEXTURE *textureNrm = nullptr;
	RT64_MATERIAL baseMaterial;
	RT64_MATERIAL frameMaterial;
	RT64_MATERIAL materialMods;
	RT64_MATRIX4 transform;
	RT64_INSTANCE *instance = nullptr;
	std::vector<RT64_VERTEX> objVertices;
	std::vector<unsigned int> objIndices;
} RT64;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if ((RT64.inspector != nullptr) && RT64.lib.HandleMessageInspector(RT64.inspector, message, wParam, lParam)) {
		return true;
	}

	switch (message) {
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		RT64.lib.SetViewPerspective(RT64.view, { 1.0f, 0.5f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0 }, (45.0f * M_PI) / 180.0f, 0.1f, 100.0f);

		RT64.lib.SetMaterialInspector(RT64.inspector, &RT64.materialMods);

		RT64.frameMaterial = RT64.baseMaterial;
		RT64_ApplyMaterialAttributes(&RT64.frameMaterial, &RT64.materialMods);

		RT64.lib.SetLightsInspector(RT64.inspector, RT64.lights, &RT64.lightCount, _countof(RT64.lights));

		RT64.lib.SetInstance(RT64.instance, RT64.mesh, RT64.transform, RT64.textureDif, RT64.textureNrm, RT64.frameMaterial, 0);
		RT64.lib.SetSceneLights(RT64.scene, RT64.lights, RT64.lightCount);
		RT64.lib.DrawDevice(RT64.device, 1);

		return 0;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

bool createRT64(HWND hwnd) {
	// Setup library.
	RT64.lib = RT64_LoadLibrary();
	if (RT64.lib.handle == 0) {
		fprintf(stderr, "Failed to load library.\n");
		return false;
	}

	// Setup device.
	RT64.device = RT64.lib.CreateDevice(hwnd);
	if (RT64.device == nullptr) {
		fprintf(stderr, "Failed to create device.\n");
		return false;
	}

	RT64.inspector = RT64.lib.CreateInspector(RT64.device);

	return true;
}

void setupRT64Scene() {
	// Setup scene.
	RT64.scene = RT64.lib.CreateScene(RT64.device);

	// Setup lights.
	// Light 0 only needs the diffuse color because it is always the ambient light.
	RT64.lights[0].diffuseColor = { 0.3f, 0.35f, 0.45f };
	RT64.lights[1].position = { 15000.0f, 30000.0f, -15000.0f };
	RT64.lights[1].attenuationRadius = 1e9;
	RT64.lights[1].pointRadius = 1000.0f;
	RT64.lights[1].diffuseColor = { 0.8f, 0.75f, 0.65f };
	RT64.lights[1].specularIntensity = 1.0f;
	RT64.lights[1].shadowOffset = 0.0f;
	RT64.lights[1].attenuationExponent = 1.0f;
	RT64.lightCount = 2;

	for (int i = 0; i < _countof(RT64.lights); i++) {
		RT64.lights[i].minSamples = 8;
		RT64.lights[i].maxSamples = 32;
		RT64.lights[i].groupBits = RT64_LIGHT_GROUP_DEFAULT;
	}

	// Setup view.
	RT64.view = RT64.lib.CreateView(RT64.scene);

	// Load texture.
	int texWidth, texHeight, texChannels;
	stbi_uc *texBytes = stbi_load("res/grass_dif.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	RT64.textureDif = RT64.lib.CreateTextureFromRGBA8(RT64.device, texBytes, texWidth, texHeight, 4);
	stbi_image_free(texBytes);

	texBytes = stbi_load("res/grass_nrm.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	RT64.textureNrm = RT64.lib.CreateTextureFromRGBA8(RT64.device, texBytes, texWidth, texHeight, 4);
	stbi_image_free(texBytes);

	// Make initial transform with a 0.1f scale.
	memset(RT64.transform.m, 0, sizeof(RT64_MATRIX4));
	RT64.transform.m[0][0] = 0.1f;
	RT64.transform.m[1][1] = 0.1f;
	RT64.transform.m[2][2] = 0.1f;
	RT64.transform.m[3][3] = 1.0f;

	// Create mesh from obj file.
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;
	bool loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "res/sphere.obj", NULL, true);
	assert(loaded);
	
	for (size_t i = 0; i < shapes.size(); i++) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
			size_t fnum = shapes[i].mesh.num_face_vertices[f];
			for (size_t v = 0; v < fnum; v++) {
				tinyobj::index_t idx = shapes[i].mesh.indices[index_offset + v];
				RT64_VERTEX vertex;
				vertex.position = { attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1], attrib.vertices[3 * idx.vertex_index + 2] };
				vertex.normal = { attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };
				vertex.uv = { acosf(vertex.normal.x), acosf(vertex.normal.y) };
				vertex.inputs[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
				vertex.inputs[1] = { 1.0f, 1.0f, 1.0f, 1.0f };
				vertex.inputs[2] = { 1.0f, 1.0f, 1.0f, 1.0f };
				vertex.inputs[3] = { 1.0f, 1.0f, 1.0f, 1.0f };

				RT64.objIndices.push_back((unsigned int)(RT64.objVertices.size()));
				RT64.objVertices.push_back(vertex);
			}

			index_offset += fnum;
		}
	}
	
	RT64.mesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED | RT64_MESH_RAYTRACE_UPDATABLE);
	RT64.lib.SetMesh(RT64.mesh, RT64.objVertices.data(), (int)(RT64.objVertices.size()), RT64.objIndices.data(), (int)(RT64.objIndices.size()));
	
	// Configure material.
	RT64.baseMaterial.filterMode = RT64_MATERIAL_FILTER_LINEAR;
	RT64.baseMaterial.hAddressMode = RT64_MATERIAL_ADDR_WRAP;
	RT64.baseMaterial.vAddressMode = RT64_MATERIAL_ADDR_WRAP;
	RT64.baseMaterial.ignoreNormalFactor = 0.0f;
	RT64.baseMaterial.normalMapScale = 1.0f;
	RT64.baseMaterial.reflectionFactor = 0.0f;
	RT64.baseMaterial.reflectionShineFactor = 0.0f;
	RT64.baseMaterial.refractionFactor = 0.0f;
	RT64.baseMaterial.specularIntensity = 1.0f;
	RT64.baseMaterial.specularExponent = 25.0f;
	RT64.baseMaterial.solidAlphaMultiplier = 1.0f;
	RT64.baseMaterial.shadowAlphaMultiplier = 1.0f;
	RT64.baseMaterial.diffuseColorMix = { 0.0f, 0.0f, 0.0f, 0.0f };
	RT64.baseMaterial.selfLight = { 0.0f , 0.0f, 0.0f };
	RT64.baseMaterial.lightGroupMaskBits = 0xFFFFFFFF;
	RT64.baseMaterial.fogColor = { 0.3f, 0.5f, 0.7f };
	RT64.baseMaterial.fogMul = 1.0f;
	RT64.baseMaterial.fogOffset = 0.0f;

	// Configure N64 Color combiner parameters.
	RT64.baseMaterial.c0[0] = 0;
	RT64.baseMaterial.c0[1] = 0;
	RT64.baseMaterial.c0[2] = 0;
	RT64.baseMaterial.c0[3] = RT64_MATERIAL_CC_SHADER_TEXEL0;
	RT64.baseMaterial.c1[0] = 0;
	RT64.baseMaterial.c1[1] = 0;
	RT64.baseMaterial.c1[2] = 0;
	RT64.baseMaterial.c1[3] = 0;
	RT64.baseMaterial.do_single[0] = 1;
	RT64.baseMaterial.do_single[1] = 0;
	RT64.baseMaterial.do_multiply[0] = 0;
	RT64.baseMaterial.do_multiply[1] = 0;
	RT64.baseMaterial.do_mix[0] = 0;
	RT64.baseMaterial.do_mix[1] = 0;
	RT64.baseMaterial.color_alpha_same = 0;
	RT64.baseMaterial.opt_alpha = 0;
	RT64.baseMaterial.opt_fog = 1;
	RT64.baseMaterial.opt_texture_edge = 0;
	
	RT64_VERTEX vertices[3];
	vertices[0].position = { -1.0f, 0.1f, 0.0f } ;
	vertices[0].normal = { 0.0f, 1.0f, 0.0f };
	vertices[0].uv = { 0.0f, 0.0f };
	vertices[0].inputs[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[0].inputs[1] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[0].inputs[2] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[0].inputs[3] = { 1.0f, 1.0f, 1.0f, 1.0f };

	vertices[1].position = { -0.5f, 0.1f, 0.0f };
	vertices[1].uv = { 1.0f, 0.0f };
	vertices[1].inputs[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[1].inputs[1] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[1].inputs[2] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[1].inputs[3] = { 1.0f, 1.0f, 1.0f, 1.0f };

	vertices[2].position = { -0.75f, 0.3f, 0.0f };
	vertices[2].uv = { 0.0f, 1.0f };
	vertices[2].inputs[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[2].inputs[1] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[2].inputs[2] = { 1.0f, 1.0f, 1.0f, 1.0f };
	vertices[2].inputs[3] = { 1.0f, 1.0f, 1.0f, 1.0f };
	
	unsigned int indices[] = { 0, 1, 2 };

	texBytes = stbi_load("res/tiles_dif.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	RT64_TEXTURE *altTexture = RT64.lib.CreateTextureFromRGBA8(RT64.device, texBytes, texWidth, texHeight, 4);
	stbi_image_free(texBytes);

	texBytes = stbi_load("res/tiles_nrm.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	RT64_TEXTURE* normalTexture = RT64.lib.CreateTextureFromRGBA8(RT64.device, texBytes, texWidth, texHeight, 4);
	stbi_image_free(texBytes);

	RT64_MESH *mesh = RT64.lib.CreateMesh(RT64.device, 0);
	RT64.lib.SetMesh(mesh, vertices, _countof(vertices), indices, _countof(indices));

	vertices[0].position.y += 0.15f;
	vertices[1].position.y += 0.15f;
	vertices[2].position.y += 0.15f;

	RT64_MESH* altMesh = RT64.lib.CreateMesh(RT64.device, 0);
	RT64.lib.SetMesh(altMesh, vertices, _countof(vertices), indices, _countof(indices));

	// Create HUD B Instance.
	RT64_INSTANCE *instanceB = RT64.lib.CreateInstance(RT64.scene);
	RT64.lib.SetInstance(instanceB, altMesh, RT64.transform, altTexture, nullptr, RT64.baseMaterial, 0);

	// Create RT Instance.
	RT64.instance = RT64.lib.CreateInstance(RT64.scene);
	RT64.lib.SetInstance(RT64.instance, RT64.mesh, RT64.transform, RT64.textureDif, RT64.textureNrm, RT64.baseMaterial, 0);

	// Create HUD A Instance.
	RT64_INSTANCE* instanceA = RT64.lib.CreateInstance(RT64.scene);
	RT64.lib.SetInstance(instanceA, mesh, RT64.transform, RT64.textureDif, nullptr, RT64.baseMaterial, RT64_INSTANCE_RASTER_BACKGROUND);

	// Create floor.
	RT64_VERTEX floorVertices[4];
	RT64_MATRIX4 floorTransform;
	unsigned int floorIndices[6] = { 2, 1, 0, 1, 2, 3 };
	floorVertices[0].position = { -1.5f, -0.15f, -1.0f };
	floorVertices[0].uv = { 0.0f, 0.0f };
	floorVertices[1].position = { 1.0f, -0.15f, -1.0f };
	floorVertices[1].uv = { 1.0f, 0.0f };
	floorVertices[2].position = { -1.5f, -0.15f, 1.0f };
	floorVertices[2].uv = { 0.0f, 1.0f };
	floorVertices[3].position = { 1.0f, -0.15f, 1.0f };
	floorVertices[3].uv = { 1.0f, 1.0f };

	for (int i = 0; i < _countof(floorVertices); i++) {
		floorVertices[i].normal = { 0.0f, 1.0f, 0.0f };
		floorVertices[i].inputs[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
		floorVertices[i].inputs[1] = { 1.0f, 1.0f, 1.0f, 1.0f };
		floorVertices[i].inputs[2] = { 1.0f, 1.0f, 1.0f, 1.0f };
		floorVertices[i].inputs[3] = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	memset(&floorTransform, 0, sizeof(RT64_MATRIX4));
	floorTransform.m[0][0] = 1.0f;
	floorTransform.m[1][1] = 1.0f;
	floorTransform.m[2][2] = 1.0f;
	floorTransform.m[3][3] = 1.0f;

	RT64_MESH* floorMesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED);
	RT64.lib.SetMesh(floorMesh, floorVertices, _countof(floorVertices), floorIndices, _countof(floorIndices));
	RT64_INSTANCE *floorInstance = RT64.lib.CreateInstance(RT64.scene);
	RT64.lib.SetInstance(floorInstance, floorMesh, floorTransform, altTexture, normalTexture, RT64.baseMaterial, 0);
}

void destroyRT64() {
	RT64.lib.DestroyDevice(RT64.device);
	RT64_UnloadLibrary(RT64.lib);
}

int main(int argc, char *argv[]) {
	// Register window class.
	WNDCLASS wc;
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandle(0);
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
	wc.lpszClassName ="RT64Sample";
	RegisterClass(&wc);

	// Create window.
	const int Width = 1280;
	const int Height = 720;
	RECT rect;
	UINT dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	rect.left = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
	rect.top = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;
	rect.right = rect.left + Width;
	rect.bottom = rect.top + Height;
	AdjustWindowRectEx(&rect, dwStyle, 0, 0);

	HWND hwnd = CreateWindow(wc.lpszClassName, "RT64 Sample", dwStyle, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0, 0, wc.hInstance, NULL);

	// Create RT64.
	if (!createRT64(hwnd)) {
		fprintf(stderr, "Failed to initialize RT64.");
		return 1;
	}

	// Setup scene in RT64.
	setupRT64Scene();

	// Window message loop.
	MSG msg = {};
	while (msg.message != WM_QUIT) {
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	destroyRT64();

	return static_cast<char>(msg.wParam);
}