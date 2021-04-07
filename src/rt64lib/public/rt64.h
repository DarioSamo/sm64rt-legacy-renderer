//
// RT64
//

// General explanation
// RT64_DEBUG if you wanna use debug Dll.

#ifndef RT64_H_INCLUDED
#define RT64_H_INCLUDED

#include <Windows.h>
#include <stdio.h>

////// TODO
// Material constants.
#define RT64_MATERIAL_FILTER_POINT				0
#define RT64_MATERIAL_FILTER_LINEAR				1
#define RT64_MATERIAL_ADDR_WRAP					0
#define RT64_MATERIAL_ADDR_MIRROR				1
#define RT64_MATERIAL_ADDR_CLAMP				2
#define RT64_MATERIAL_CC_SHADER_0				0
#define RT64_MATERIAL_CC_SHADER_INPUT_1			1
#define RT64_MATERIAL_CC_SHADER_INPUT_2			2
#define RT64_MATERIAL_CC_SHADER_INPUT_3			3
#define RT64_MATERIAL_CC_SHADER_INPUT_4			4
#define RT64_MATERIAL_CC_SHADER_TEXEL0			5
#define RT64_MATERIAL_CC_SHADER_TEXEL0A			6
#define RT64_MATERIAL_CC_SHADER_TEXEL1			7

////// TODO
// Material attributes.
#define RT64_ATTRIBUTE_NONE						0x0
#define RT64_ATTRIBUTE_IGNORE_NORMAL_FACTOR		0x1
#define RT64_ATTRIBUTE_NORMAL_MAP_STRENGTH		0x2
#define RT64_ATTRIBUTE_NORMAL_MAP_SCALE			0x4
#define RT64_ATTRIBUTE_REFLECTION_FACTOR		0x8
#define RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR	0x10
#define RT64_ATTRIBUTE_REFRACTION_FACTOR		0x20
#define RT64_ATTRIBUTE_SPECULAR_INTENSITY		0x40
#define RT64_ATTRIBUTE_SPECULAR_EXPONENT		0x80
#define RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER	0x100
#define RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER	0x200
#define RT64_ATTRIBUTE_SELF_LIGHT				0x400
#define RT64_ATTRIBUTE_DIFFUSE_COLOR_MIX		0x800

////// TODO
// Mesh flags.
#define RT64_MESH_RAYTRACE_ENABLED				0x1
#define RT64_MESH_RAYTRACE_UPDATABLE			0x2

////// TODO
// Vector 2
typedef struct {
	float x, y;
} RT64_VECTOR2;

////// TODO
// Vector 3
typedef struct {
	float x, y, z;
} RT64_VECTOR3;

////// TODO
// Vector 4
typedef struct {
	float x, y, z, w;
} RT64_VECTOR4;

////// TODO
// Matrix 4
typedef struct {
	float m[4][4];
} RT64_MATRIX4;

////// TODO
// Vertex
typedef struct {
	RT64_VECTOR3 position;
	RT64_VECTOR3 normal;
	RT64_VECTOR2 uv;
	RT64_VECTOR4 inputs[4];
} RT64_VERTEX;

////// TODO
// Material
typedef struct {
	////// TODO
	int background;

	////// TODO
	int filterMode;

	////// TODO
	int diffuseTexIndex;

	////// TODO
	int normalTexIndex;

	////// TODO
	int hAddressMode;

	////// TODO
	int vAddressMode;

	////// TODO
	float ignoreNormalFactor;

	////// TODO
	float normalMapStrength;

	////// TODO
	float normalMapScale;

	////// TODO
	float reflectionFactor;

	////// TODO
	float reflectionShineFactor;

	////// TODO
	float refractionFactor;

	////// TODO
	float specularIntensity;

	////// TODO
	float specularExponent;

	////// TODO
	float solidAlphaMultiplier;

	////// TODO
	float shadowAlphaMultiplier;

	////// TODO
	RT64_VECTOR3 selfLight;

	////// TODO
	RT64_VECTOR3 fogColor;

	////// TODO
	RT64_VECTOR4 diffuseColorMix;

	////// TODO
	float fogMul;

	////// TODO
	float fogOffset;

	// N64 Color combiner parameters.
	int c0[4];
	int c1[4];
	int do_single[2];
	int do_multiply[2];
	int do_mix[2];
	int color_alpha_same;
	int opt_alpha;
	int opt_fog;
	int opt_texture_edge;

	// Flag containing all attributes that are actually used by this material.
	int enabledAttributes;

	// Add padding to line up with the HLSL structure.
	int _pad;
} RT64_MATERIAL;

////// TODO
// Light
typedef struct {
	////// TODO
	RT64_VECTOR3 position;

	////// TODO
	RT64_VECTOR3 diffuseColor;

	////// TODO
	float attenuationRadius;

	////// TODO
	float pointRadius;

	////// TODO
	float specularIntensity;

	////// TODO
	float shadowOffset;

	////// TODO
	float attenuationExponent;

	////// TODO
	float flickerIntensity;
} RT64_LIGHT;


////// TODO
// Specialized types.
typedef struct RT64_DEVICE RT64_DEVICE;
typedef struct RT64_VIEW RT64_VIEW;
typedef struct RT64_SCENE RT64_SCENE;
typedef struct RT64_INSTANCE RT64_INSTANCE;
typedef struct RT64_MESH RT64_MESH;
typedef struct RT64_TEXTURE RT64_TEXTURE;
typedef struct RT64_INSPECTOR RT64_INSPECTOR;

////// TODO
// Description.
// @param param_name param_description
// @returns Something.
inline void RT64_ApplyMaterialAttributes(RT64_MATERIAL *dst, RT64_MATERIAL *src) {
	if (src->enabledAttributes & RT64_ATTRIBUTE_IGNORE_NORMAL_FACTOR) {
		dst->ignoreNormalFactor = src->ignoreNormalFactor;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_NORMAL_MAP_STRENGTH) {
		dst->normalMapStrength = src->normalMapStrength;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_NORMAL_MAP_SCALE) {
		dst->normalMapScale = src->normalMapScale;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_REFLECTION_FACTOR) {
		dst->reflectionFactor = src->reflectionFactor;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR) {
		dst->reflectionShineFactor = src->reflectionShineFactor;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_REFRACTION_FACTOR) {
		dst->refractionFactor = src->refractionFactor;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_SPECULAR_INTENSITY) {
		dst->specularIntensity = src->specularIntensity;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_SPECULAR_EXPONENT) {
		dst->specularExponent = src->specularExponent;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER) {
		dst->solidAlphaMultiplier = src->solidAlphaMultiplier;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER) {
		dst->shadowAlphaMultiplier = src->shadowAlphaMultiplier;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_SELF_LIGHT) {
		dst->selfLight = src->selfLight;
	}

	if (src->enabledAttributes & RT64_ATTRIBUTE_DIFFUSE_COLOR_MIX) {
		dst->diffuseColorMix = src->diffuseColorMix;
	}
}

// Internal function pointer types.
typedef RT64_DEVICE* (*CreateDevicePtr)(void *hwnd);
typedef void(*DrawDevicePtr)(RT64_DEVICE* device, int vsyncInterval);
typedef void(*DestroyDevicePtr)(RT64_DEVICE* device);
typedef RT64_VIEW* (*CreateViewPtr)(RT64_SCENE* scenePtr);
typedef void(*SetViewPerspectivePtr)(RT64_VIEW* viewPtr, RT64_VECTOR3 eyePosition, RT64_VECTOR3 eyeFocus, RT64_VECTOR3 eyeUpDirection, float fovRadians, float nearDist, float farDist);
typedef void(*DestroyViewPtr)(RT64_VIEW* viewPtr);
typedef RT64_SCENE* (*CreateScenePtr)(RT64_DEVICE* devicePtr);
typedef void (*SetSceneLightsPtr)(RT64_SCENE* scenePtr, RT64_LIGHT* lightArray, int lightCount);
typedef void(*DestroyScenePtr)(RT64_SCENE* scenePtr);
typedef RT64_MESH* (*CreateMeshPtr)(RT64_DEVICE* devicePtr, int flags);
typedef void (*SetMeshPtr)(RT64_MESH* meshPtr, RT64_VERTEX* vertexArray, int vertexCount, unsigned int* indexArray, int indexCount);
typedef void (*DestroyMeshPtr)(RT64_MESH* meshPtr);
typedef RT64_INSTANCE* (*CreateInstancePtr)(RT64_SCENE* scenePtr);
typedef void (*SetInstancePtr)(RT64_INSTANCE* instancePtr, RT64_MESH* meshPtr, RT64_MATRIX4 transform, RT64_TEXTURE* diffuseTexture, RT64_TEXTURE* normalTexture, RT64_MATERIAL material);
typedef void (*DestroyInstancePtr)(RT64_INSTANCE* instancePtr);
typedef RT64_TEXTURE* (*CreateTextureFromRGBA8Ptr)(RT64_DEVICE* devicePtr, void* bytes, int width, int height, int stride);
typedef void(*DestroyTexturePtr)(RT64_TEXTURE* texture);
typedef RT64_INSPECTOR* (*CreateInspectorPtr)(RT64_DEVICE* devicePtr);
typedef bool(*HandleMessageInspectorPtr)(RT64_INSPECTOR* inspectorPtr, UINT msg, WPARAM wParam, LPARAM lParam);
typedef void (*SetMaterialInspectorPtr)(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material);
typedef void(*SetLightsInspectorPtr)(RT64_INSPECTOR* inspectorPtr, RT64_LIGHT* lights, int *lightCount, int maxLightCount);
typedef void(*PrintToInspectorPtr)(RT64_INSPECTOR* inspectorPtr, const char* message);
typedef void(*DestroyInspectorPtr)(RT64_INSPECTOR* inspectorPtr);

// Stores all the function pointers used in the RT64 library. Variables inside this structure can be called directly
// once it's created by the RT64_LoadLibrary() function.
typedef struct {
	// Handle to the DLL module. Zero if it's empty and the library could not be loaded successfully.
	HMODULE handle;

	// Creates an RT64 device that can render to the specified window.
	// @param hwnd Window handle directly from the Windows API (HWND).
	// @returns A pointer to an RT64_DEVICE that is required by most of the other methods.
	CreateDevicePtr CreateDevice;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DrawDevicePtr DrawDevice;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyDevicePtr DestroyDevice;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	CreateViewPtr CreateView;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	SetViewPerspectivePtr SetViewPerspective;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyViewPtr DestroyView;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	CreateScenePtr CreateScene;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	SetSceneLightsPtr SetSceneLights;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyScenePtr DestroyScene;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	CreateMeshPtr CreateMesh;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	SetMeshPtr SetMesh;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyMeshPtr DestroyMesh;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	CreateInstancePtr CreateInstance;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	SetInstancePtr SetInstance;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyInstancePtr DestroyInstance;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	CreateTextureFromRGBA8Ptr CreateTextureFromRGBA8;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyTexturePtr DestroyTexture;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	CreateInspectorPtr CreateInspector;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	HandleMessageInspectorPtr HandleMessageInspector;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	PrintToInspectorPtr PrintToInspector;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	SetMaterialInspectorPtr SetMaterialInspector;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	SetLightsInspectorPtr SetLightsInspector;

	////// TODO
	// Description.
	// @param param_name param_description
	// @returns Something.
	DestroyInspectorPtr DestroyInspector;
} RT64_LIBRARY;

////// TODO
// Description.
// @param param_name param_description
// @returns Something.
inline RT64_LIBRARY RT64_LoadLibrary() {
	RT64_LIBRARY lib;
#ifdef RT64_DEBUG
	lib.handle = LoadLibrary(TEXT("rt64libd.dll"));
#else
	lib.handle = LoadLibrary(TEXT("rt64lib.dll"));
#endif
	if (lib.handle != 0) {
		lib.CreateDevice = (CreateDevicePtr)(GetProcAddress(lib.handle, "RT64_CreateDevice"));
		lib.DrawDevice = (DrawDevicePtr)(GetProcAddress(lib.handle, "RT64_DrawDevice"));
		lib.DestroyDevice = (DestroyDevicePtr)(GetProcAddress(lib.handle, "RT64_DestroyDevice"));
		lib.CreateView = (CreateViewPtr)(GetProcAddress(lib.handle, "RT64_CreateView"));
		lib.SetViewPerspective = (SetViewPerspectivePtr)(GetProcAddress(lib.handle, "RT64_SetViewPerspective"));
		lib.DestroyView = (DestroyViewPtr)(GetProcAddress(lib.handle, "RT64_DestroyView"));
		lib.CreateScene = (CreateScenePtr)(GetProcAddress(lib.handle, "RT64_CreateScene"));
		lib.SetSceneLights = (SetSceneLightsPtr)(GetProcAddress(lib.handle, "RT64_SetSceneLights"));
		lib.DestroyScene = (DestroyScenePtr)(GetProcAddress(lib.handle, "RT64_DestroyScene"));
		lib.CreateMesh = (CreateMeshPtr)(GetProcAddress(lib.handle, "RT64_CreateMesh"));
		lib.SetMesh = (SetMeshPtr)(GetProcAddress(lib.handle, "RT64_SetMesh"));
		lib.DestroyMesh = (DestroyMeshPtr)(GetProcAddress(lib.handle, "RT64_DestroyMesh"));
		lib.CreateInstance = (CreateInstancePtr)(GetProcAddress(lib.handle, "RT64_CreateInstance"));
		lib.SetInstance = (SetInstancePtr)(GetProcAddress(lib.handle, "RT64_SetInstance"));
		lib.DestroyInstance = (DestroyInstancePtr)(GetProcAddress(lib.handle, "RT64_DestroyInstance"));
		lib.CreateTextureFromRGBA8 = (CreateTextureFromRGBA8Ptr)(GetProcAddress(lib.handle, "RT64_CreateTextureFromRGBA8"));
		lib.DestroyTexture = (DestroyTexturePtr)(GetProcAddress(lib.handle, "RT64_DestroyTexture"));
		lib.CreateInspector = (CreateInspectorPtr)(GetProcAddress(lib.handle, "RT64_CreateInspector"));
		lib.HandleMessageInspector = (HandleMessageInspectorPtr)(GetProcAddress(lib.handle, "RT64_HandleMessageInspector"));
		lib.SetMaterialInspector = (SetMaterialInspectorPtr)(GetProcAddress(lib.handle, "RT64_SetMaterialInspector"));
		lib.SetLightsInspector = (SetLightsInspectorPtr)(GetProcAddress(lib.handle, "RT64_SetLightsInspector"));
		lib.PrintToInspector = (PrintToInspectorPtr)(GetProcAddress(lib.handle, "RT64_PrintToInspector"));
		lib.DestroyInspector = (DestroyInspectorPtr)(GetProcAddress(lib.handle, "RT64_DestroyInspector"));
	}

	return lib;
}

////// TODO
// Description.
// @param param_name param_description
inline void RT64_UnloadLibrary(RT64_LIBRARY lib) {
	FreeLibrary(lib.handle);
}

#endif