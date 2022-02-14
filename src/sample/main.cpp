//
// RT64 SAMPLE
//

#ifndef NDEBUG
#	define RT64_DEBUG
#endif

#include "rt64.h"

#define WINDOW_TITLE "RT64 Sample"

#include <Windows.h>

static void infoMessage(HWND hWnd, const char *message) {
	MessageBox(hWnd, message, WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
}

static void errorMessage(HWND hWnd, const char *message) {
	MessageBox(hWnd, message, WINDOW_TITLE " Error", MB_OK | MB_ICONERROR);
}

#ifndef RT64_MINIMAL

#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

typedef struct {
	RT64_VECTOR4 position;
	RT64_VECTOR3 normal;
	RT64_VECTOR2 uv;
	RT64_VECTOR4 input1;
} VERTEX;

struct {
	RT64_LIBRARY lib;
	RT64_LIGHT lights[16];
	int lightCount;
	RT64_DEVICE *device = nullptr;
	RT64_INSPECTOR* inspector = nullptr;
	RT64_SCENE *scene = nullptr;
	RT64_SCENE_DESC sceneDesc;
	RT64_VIEW *view = nullptr;
	RT64_MATRIX4 viewMatrix;
	RT64_MESH *mesh = nullptr;
	RT64_SHADER *shader = nullptr;
	RT64_TEXTURE *textureDif = nullptr;
	RT64_TEXTURE *textureNrm = nullptr;
	RT64_TEXTURE *textureSpc = nullptr;
	RT64_TEXTURE *textureEms = nullptr;
	RT64_TEXTURE *textureRgh = nullptr;
	RT64_TEXTURE *textureMtl = nullptr;
	RT64_TEXTURE *textureAmb = nullptr;
	RT64_MATERIAL baseMaterial;
	RT64_MATERIAL frameMaterial;
	RT64_MATERIAL materialMods;
	RT64_MATRIX4 transform;
	RT64_INSTANCE *instance = nullptr;
	std::vector<VERTEX> objVertices;
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
	case WM_RBUTTONDOWN: {
		POINT cursorPos = {};
		GetCursorPos(&cursorPos);
		ScreenToClient(hWnd, &cursorPos);
		RT64_INSTANCE *instance = RT64.lib.GetViewRaytracedInstanceAt(RT64.view, cursorPos.x, cursorPos.y);
		fprintf(stdout, "GetViewRaytracedInstanceAt: %p\n", instance);
		break;
	}
	case WM_KEYDOWN: {
		if (wParam == VK_F1) {
			if (RT64.inspector != nullptr) {
				RT64.lib.DestroyInspector(RT64.inspector);
				RT64.inspector = nullptr;
			}
			else {
				RT64.inspector = RT64.lib.CreateInspector(RT64.device);
			}
		}

		break;
	}
	case WM_PAINT: {
		if (RT64.view != nullptr) {
			RT64.lib.SetViewPerspective(RT64.view, RT64.viewMatrix, (45.0f * (float)(M_PI)) / 180.0f, 0.1f, 1000.0f, true);

			if (RT64.inspector != nullptr) {
				RT64.lib.SetMaterialInspector(RT64.inspector, &RT64.materialMods, "Sphere");
				RT64.lib.SetSceneInspector(RT64.inspector, &RT64.sceneDesc);
				RT64.lib.SetSceneDescription(RT64.scene, RT64.sceneDesc);
			}

			RT64.frameMaterial = RT64.baseMaterial;
			RT64_ApplyMaterialAttributes(&RT64.frameMaterial, &RT64.materialMods);

			if (RT64.inspector != nullptr) {
				RT64.lib.SetLightsInspector(RT64.inspector, RT64.lights, &RT64.lightCount, _countof(RT64.lights));
			}

			RT64_INSTANCE_DESC instDesc;
			instDesc.scissorRect = { 0, 0, 0, 0 };
			instDesc.viewportRect = { 0, 0, 0, 0 };
			instDesc.mesh = RT64.mesh;
			instDesc.transform = RT64.transform;
			instDesc.previousTransform = RT64.transform;
			instDesc.diffuseTexture = RT64.textureDif;
			instDesc.normalTexture = RT64.textureNrm;
			instDesc.specularTexture = RT64.textureSpc;
			instDesc.emissiveTexture = RT64.textureEms;
			instDesc.roughnessTexture = RT64.textureRgh;
			instDesc.metalnessTexture = RT64.textureMtl;
			instDesc.ambientTexture = RT64.textureAmb;
			instDesc.material = RT64.frameMaterial;
			instDesc.shader = RT64.shader;
			instDesc.flags = 0;

			RT64.lib.SetInstanceDescription(RT64.instance, instDesc);
			RT64.lib.SetSceneLights(RT64.scene, RT64.lights, RT64.lightCount);
			RT64.lib.DrawDevice(RT64.device, 1);
			return 0;
		}
	}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

bool createRT64(HWND hwnd) {
	// Setup library.
	RT64.lib = RT64_LoadLibrary();
	if (RT64.lib.handle == 0) {
		errorMessage(hwnd, "Failed to load RT64 library.");
		return false;
	}

	// Setup device.
	RT64.device = RT64.lib.CreateDevice(hwnd);
	if (RT64.device == nullptr) {
		errorMessage(hwnd, RT64.lib.GetLastError());
		return false;
	}

	return true;
}

RT64_TEXTURE *loadTexturePNG(const char *path) {
	RT64_TEXTURE_DESC texDesc;
	int texChannels;
	texDesc.format = RT64_TEXTURE_FORMAT_RGBA8;
	texDesc.bytes = stbi_load(path, &texDesc.width, &texDesc.height, &texChannels, STBI_rgb_alpha);
	texDesc.rowPitch = texDesc.width * 4;
	texDesc.byteCount = texDesc.rowPitch * texDesc.height;
	RT64_TEXTURE *texture = RT64.lib.CreateTexture(RT64.device, texDesc);
	stbi_image_free((void *)(texDesc.bytes));
	return texture;
}

RT64_TEXTURE *loadTextureDDS(const char *path) {
	RT64_TEXTURE *texture = nullptr;
	FILE *ddsFp = stbi__fopen(path, "rb");
	if (ddsFp != nullptr) {
		fseek(ddsFp, 0, SEEK_END);
		int ddsDataSize = ftell(ddsFp);
		fseek(ddsFp, 0, SEEK_SET);
		if (ddsDataSize > 0) {
			void *ddsData = malloc(ddsDataSize);
			fread(ddsData, ddsDataSize, 1, ddsFp);
			fclose(ddsFp);

			RT64_TEXTURE_DESC texDesc;
			texDesc.bytes = ddsData;
			texDesc.byteCount = ddsDataSize;
			texDesc.format = RT64_TEXTURE_FORMAT_DDS;
			texDesc.width = texDesc.height = texDesc.rowPitch = -1;
			texture = RT64.lib.CreateTexture(RT64.device, texDesc);
			free(ddsData);
		}
		else {
			fclose(ddsFp);
		}
	}

	return texture;
}

void setupRT64Scene() {
	// Setup scene.
	RT64.scene = RT64.lib.CreateScene(RT64.device);
	RT64.sceneDesc.ambientBaseColor = { 0.1f, 0.1f, 0.1f };
	RT64.sceneDesc.ambientNoGIColor = { 0.2f, 0.2f, 0.2f };
	RT64.sceneDesc.eyeLightDiffuseColor = { 0.08f, 0.08f, 0.08f };
	RT64.sceneDesc.eyeLightSpecularColor = { 0.04f, 0.04f, 0.04f };
	RT64.sceneDesc.skyDiffuseMultiplier = { 1.0f, 1.0f, 1.0f };
	RT64.sceneDesc.skyHSLModifier = { 0.0f, 0.0f, 0.0f };
	RT64.sceneDesc.skyYawOffset = 0.0f;
	RT64.sceneDesc.giDiffuseStrength = 0.7f;
	RT64.sceneDesc.giSkyStrength = 0.35f;

	RT64.sceneDesc.ambientFogColor = { 0.1f, 0.25f, 2.0f };
	RT64.sceneDesc.ambientFogAlpha = 0.2f;
	RT64.sceneDesc.ambientFogFactors = { 1250.0f, 100.0f };
	RT64.sceneDesc.groundFogColor = { 0.5f, 0.65f, 0.85f };
	RT64.sceneDesc.groundFogAlpha = 0.375f;
	RT64.sceneDesc.groundFogFactors = { 250.5f, 50.f };
	RT64.sceneDesc.groundFogHeightFactors = { 75.0f, 50.0f };
	RT64.lib.SetSceneDescription(RT64.scene, RT64.sceneDesc);

	// Setup shader.
	int shaderFlags = RT64_SHADER_RASTER_ENABLED | RT64_SHADER_RAYTRACE_ENABLED | RT64_SHADER_NORMAL_MAP_ENABLED | RT64_SHADER_SPECULAR_MAP_ENABLED | RT64_SHADER_EMISSIVE_MAP_ENABLED | RT64_SHADER_ROUGHNESS_MAP_ENABLED | RT64_SHADER_METALNESS_MAP_ENABLED | RT64_SHADER_AMBIENT_MAP_ENABLED;
	RT64.shader = RT64.lib.CreateShader(RT64.device, 0x01200a00, RT64_SHADER_FILTER_LINEAR, RT64_SHADER_ADDRESSING_WRAP, RT64_SHADER_ADDRESSING_WRAP, shaderFlags);

	// Setup lights.
	RT64.lights[0].position = { 15000.0f, 30000.0f, 15000.0f };
	RT64.lights[0].attenuationRadius = 1e9;
	RT64.lights[0].pointRadius = 5000.0f;
	RT64.lights[0].diffuseColor = { 0.8f, 0.75f, 0.65f };
	RT64.lights[0].specularColor = { 0.8f, 0.75f, 0.65f };
	RT64.lights[0].shadowOffset = 0.0f;
	RT64.lights[0].attenuationExponent = 1.0f;
	RT64.lightCount = 1;

	for (int i = 0; i < _countof(RT64.lights); i++) {
		RT64.lights[i].groupBits = RT64_LIGHT_GROUP_DEFAULT;
	}

	// Setup view.
	RT64.view = RT64.lib.CreateView(RT64.scene);

	// Load textures.
	RT64.textureDif = loadTexturePNG("res/grass_dif.png");
	RT64.textureNrm = nullptr;
	RT64.textureSpc = nullptr;
	RT64.textureEms = nullptr;
	RT64.textureRgh = nullptr;
	RT64.textureMtl = nullptr;
	RT64.textureAmb = nullptr;
	RT64_TEXTURE *textureSky = loadTexturePNG("res/clouds.png");
	RT64.lib.SetViewSkyPlane(RT64.view, textureSky);

	// Make initial transform with a 0.1f scale.
	memset(RT64.transform.m, 0, sizeof(RT64_MATRIX4));
	RT64.transform.m[0][0] = 1.0f;
	RT64.transform.m[1][1] = 1.0f;
	RT64.transform.m[2][2] = 1.0f;
	RT64.transform.m[3][3] = 1.0f;

	// Make initial view.
	memset(RT64.viewMatrix.m, 0, sizeof(RT64_MATRIX4));
	RT64.viewMatrix.m[0][0] = 1.0f;
	RT64.viewMatrix.m[1][1] = 1.0f;
	RT64.viewMatrix.m[2][2] = 1.0f;
	RT64.viewMatrix.m[3][0] = 0.0f;
	RT64.viewMatrix.m[3][1] = -2.0f;
	RT64.viewMatrix.m[3][2] = -10.0f;
	RT64.viewMatrix.m[3][3] = 1.0f;

	// Create mesh from obj file.
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;
	bool loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, "res/teapot.obj", nullptr, true);
	assert(loaded);
	
	float size = 0.25;
	float yOffset = 0;
	for (size_t i = 0; i < shapes.size(); i++) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[i].mesh.num_face_vertices.size(); f++) {
			size_t fnum = shapes[i].mesh.num_face_vertices[f];
			for (size_t v = 0; v < fnum; v++) {
				tinyobj::index_t idx = shapes[i].mesh.indices[index_offset + v];
				VERTEX vertex;
				vertex.position = { attrib.vertices[3 * idx.vertex_index + 0] * size, attrib.vertices[3 * idx.vertex_index + 1] * size + yOffset, attrib.vertices[3 * idx.vertex_index + 2] * size, 1.0f };
				vertex.normal = { attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1], attrib.normals[3 * idx.normal_index + 2] };
				vertex.uv = { acosf(vertex.normal.x), acosf(vertex.normal.y) };
				vertex.input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

				RT64.objIndices.push_back((unsigned int)(RT64.objVertices.size()));
				RT64.objVertices.push_back(vertex);
			}
			index_offset += fnum;
		}
		// RT64.textureDif = materials[i];
	}
	
	RT64.mesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED | RT64_MESH_RAYTRACE_FAST_TRACE | RT64_MESH_RAYTRACE_COMPACT);
	RT64.lib.SetMesh(RT64.mesh, RT64.objVertices.data(), (int)(RT64.objVertices.size()), sizeof(VERTEX), RT64.objIndices.data(), (int)(RT64.objIndices.size()));
	
	// Configure material.
	RT64.baseMaterial.ignoreNormalFactor = 0.0f;
	RT64.baseMaterial.uvDetailScale = 1.0f;
	RT64.baseMaterial.reflectionFactor = 0.0f;
	RT64.baseMaterial.reflectionFresnelFactor = 0.5f;
	RT64.baseMaterial.reflectionShineFactor = 0.0f;
	RT64.baseMaterial.refractionFactor = 0.0f;
	RT64.baseMaterial.specularColor = { 1.0f, 1.0f, 1.0f };
	RT64.baseMaterial.specularExponent = 1.0f;
	RT64.baseMaterial.specularFresnelFactor = 0.1f;
	RT64.baseMaterial.roughnessFactor = 0.0f;
	RT64.baseMaterial.metallicFactor = 0.0f;
	RT64.baseMaterial.solidAlphaMultiplier = 1.0f;
	RT64.baseMaterial.shadowAlphaMultiplier = 1.0f;
	RT64.baseMaterial.diffuseColorMix = { 0.0f, 0.0f, 0.0f, 0.0f };
	RT64.baseMaterial.selfLight = { 0.0f , 0.0f, 0.0f };
	RT64.baseMaterial.lightGroupMaskBits = 0xFFFFFFFF;
	RT64.baseMaterial.fogColor = { 0.3f, 0.5f, 0.7f };
	RT64.baseMaterial.fogMul = 1.0f;
	RT64.baseMaterial.fogOffset = 0.0f;
	RT64.baseMaterial.fogEnabled = 0;
	
	VERTEX vertices[3];
	vertices[0].position = { -1.0f, 0.1f, 0.0f, 1.0f } ;
	vertices[0].normal = { 0.0f, 1.0f, 0.0f };
	vertices[0].uv = { 0.0f, 0.0f };
	vertices[0].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

	vertices[1].position = { -0.5f, 0.1f, 0.0f, 1.0f };
	vertices[1].normal = { 0.0f, 1.0f, 0.0f };
	vertices[1].uv = { 1.0f, 0.0f };
	vertices[1].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };

	vertices[2].position = { -0.75f, 0.3f, 0.0f, 1.0f };
	vertices[2].normal = { 0.0f, 1.0f, 0.0f };
	vertices[2].uv = { 0.0f, 1.0f };
	vertices[2].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };
	
	unsigned int indices[] = { 0, 1, 2 };
	RT64_TEXTURE *altTexture = loadTexturePNG("res/tiles_dif.png");
	RT64_TEXTURE* normalTexture = loadTexturePNG("res/tiles_nrm.png");
	RT64_TEXTURE* specularTexture = loadTexturePNG("res/tiles_spc.png");
	RT64_TEXTURE* emissiveTexture = nullptr;
	RT64_TEXTURE* roughnessTexture = nullptr;
	RT64_TEXTURE* metalnessTexture = nullptr;
	RT64_TEXTURE* ambientTexture = nullptr;

	RT64_MESH *mesh = RT64.lib.CreateMesh(RT64.device, 0);
	RT64.lib.SetMesh(mesh, vertices, _countof(vertices), sizeof(VERTEX), indices, _countof(indices));

	vertices[0].position.y += 0.15f;
	vertices[1].position.y += 0.15f;
	vertices[2].position.y += 0.15f;

	RT64_MESH* altMesh = RT64.lib.CreateMesh(RT64.device, 0);
	RT64.lib.SetMesh(altMesh, vertices, _countof(vertices), sizeof(VERTEX), indices, _countof(indices));

	RT64_INSTANCE_DESC instDesc;
	instDesc.scissorRect = { 0, 0, 0, 0 };
	instDesc.viewportRect = { 0, 0, 0, 0 };
	instDesc.mesh = altMesh;
	instDesc.transform = RT64.transform;
	instDesc.previousTransform = RT64.transform;
	instDesc.diffuseTexture = altTexture;
	instDesc.normalTexture = nullptr;
	instDesc.specularTexture = nullptr;
	instDesc.emissiveTexture = nullptr;
	instDesc.roughnessTexture = nullptr;
	instDesc.metalnessTexture = nullptr;
	instDesc.ambientTexture = nullptr;
	instDesc.material = RT64.baseMaterial;
	instDesc.shader = RT64.shader;
	instDesc.flags = 0;

	// Create HUD B Instance.
	RT64_INSTANCE *instanceB = RT64.lib.CreateInstance(RT64.scene);
	RT64.lib.SetInstanceDescription(instanceB, instDesc);

	// Create RT Instance.
	RT64.instance = RT64.lib.CreateInstance(RT64.scene);
	instDesc.mesh = RT64.mesh;
	instDesc.diffuseTexture = RT64.textureDif;
	instDesc.normalTexture = RT64.textureNrm;
	instDesc.specularTexture = RT64.textureSpc;
	instDesc.emissiveTexture = nullptr;
	instDesc.roughnessTexture = nullptr;
	instDesc.metalnessTexture = nullptr;
	instDesc.ambientTexture = nullptr;
	RT64.lib.SetInstanceDescription(RT64.instance, instDesc);

	// Create HUD A Instance.
	RT64_INSTANCE* instanceA = RT64.lib.CreateInstance(RT64.scene);
	instDesc.mesh = mesh;
	instDesc.normalTexture = nullptr;
	instDesc.specularTexture = nullptr;
	instDesc.emissiveTexture = nullptr;
	instDesc.roughnessTexture = nullptr;
	instDesc.metalnessTexture = nullptr;
	instDesc.ambientTexture = nullptr;
	instDesc.flags = RT64_INSTANCE_RASTER_BACKGROUND;
	RT64.lib.SetInstanceDescription(instanceA, instDesc);

	// Create floor.
	VERTEX floorVertices[4];
	RT64_MATRIX4 floorTransform;
	unsigned int floorIndices[6] = { 2, 1, 0, 1, 2, 3 };
	floorVertices[0].position = { -1.5f, 0.0f, -1.0f, 1.0f };
	floorVertices[0].uv = { 0.0f, 0.0f };
	floorVertices[1].position = { 1.0f, 0.0f, -1.0f, 1.0f };
	floorVertices[1].uv = { 1.0f, 0.0f };
	floorVertices[2].position = { -1.5f, 0.0f, 1.0f, 1.0f };
	floorVertices[2].uv = { 0.0f, 1.0f };
	floorVertices[3].position = { 1.0f, 0.0f, 1.0f, 1.0f };
	floorVertices[3].uv = { 1.0f, 1.0f };

	for (int i = 0; i < _countof(floorVertices); i++) {
		floorVertices[i].normal = { 0.0f, 1.0f, 0.0f };
		floorVertices[i].input1 = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	memset(&floorTransform, 0, sizeof(RT64_MATRIX4));
	floorTransform.m[0][0] = 10.0f;
	floorTransform.m[1][1] = 10.0f;
	floorTransform.m[2][2] = 10.0f;
	floorTransform.m[3][3] = 1.0f;

	RT64_MESH* floorMesh = RT64.lib.CreateMesh(RT64.device, RT64_MESH_RAYTRACE_ENABLED);
	RT64.lib.SetMesh(floorMesh, floorVertices, _countof(floorVertices), sizeof(VERTEX), floorIndices, _countof(floorIndices));
	RT64_INSTANCE *floorInstance = RT64.lib.CreateInstance(RT64.scene);
	instDesc.mesh = floorMesh;
	instDesc.transform = floorTransform;
	instDesc.previousTransform = floorTransform;
	instDesc.diffuseTexture = altTexture;
	instDesc.normalTexture = normalTexture;
	instDesc.specularTexture = specularTexture;
	instDesc.emissiveTexture = emissiveTexture;
	instDesc.roughnessTexture = roughnessTexture;
	instDesc.metalnessTexture = metalnessTexture;
	instDesc.ambientTexture = ambientTexture;
	instDesc.shader = RT64.shader;
	instDesc.flags = 0;
	RT64.lib.SetInstanceDescription(floorInstance, instDesc);
}

void destroyRT64() {
	RT64.lib.DestroyDevice(RT64.device);
	RT64_UnloadLibrary(RT64.lib);
}

int main(int argc, char *argv[]) {
	// Show a basic message to the user so they know what the sample is meant to do.
	infoMessage(NULL, 
		"This sample application will test if your system has the required hardware to run RT64.\n\n"
		"If you see some shapes in the screen after pressing OK, then you're good to go!");

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

	HWND hwnd = CreateWindow(wc.lpszClassName, WINDOW_TITLE, dwStyle, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0, 0, wc.hInstance, NULL);

	// Create RT64.
	if (!createRT64(hwnd)) {
		errorMessage(hwnd,
			"Failed to initialize RT64.\n\n"
			"Please make sure your GPU drivers are up to date and the Direct3D 12.1 feature level is supported.\n\n"
			"Windows 10 version 2004 or newer is also required for this feature level to work properly.\n\n"
			"If you're a mobile user, make sure that the high performance device is selected for this application on your system's settings.");

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

#else

// Minimal sample that only verifies if a raytracing device can be detected.

int main(int argc, char *argv[]) {
	RT64_LIBRARY lib = RT64_LoadLibrary();
	if (lib.handle == 0) {
		errorMessage(NULL, "Failed to load RT64 library.");
		return 1;
	}

	RT64_DEVICE *device = lib.CreateDevice(0);
	if (device == nullptr) {
		errorMessage(NULL, lib.GetLastError());
		return 1;
	}

	infoMessage(NULL, "Raytracing device was detected!");
	return 0;
}

#endif
