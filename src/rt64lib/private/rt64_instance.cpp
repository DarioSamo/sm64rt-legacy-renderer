//
// RT64
//

#ifndef RT64_MINIMAL

#include "../public/rt64.h"
#include "rt64_instance.h"
#include "rt64_scene.h"

// Private

const RT64_MATERIAL DefaultMaterial;

RT64::Instance::Instance(Scene *scene) {
	assert(scene != nullptr);

	this->scene = scene;
	mesh = nullptr;
	diffuseTexture = nullptr;
	normalTexture = nullptr;
	transform = XMMatrixIdentity();
	material = DefaultMaterial;
	scissorRect = { 0, 0, 0, 0 };
	viewportRect = { 0, 0, 0, 0 };
	flags = 0;

	scene->addInstance(this);
}

RT64::Instance::~Instance() {
	scene->removeInstance(this);
}

void RT64::Instance::setMesh(Mesh* mesh) {
	this->mesh = mesh;
}

RT64::Mesh* RT64::Instance::getMesh() const {
	return mesh;
}

void RT64::Instance::setMaterial(const RT64_MATERIAL &material) {
	this->material = material;
}

const RT64_MATERIAL &RT64::Instance::getMaterial() const {
	return material;
}

void RT64::Instance::setDiffuseTexture(Texture *texture) {
	this->diffuseTexture = texture;
}

RT64::Texture *RT64::Instance::getDiffuseTexture() const {
	return diffuseTexture;
}

void RT64::Instance::setNormalTexture(Texture* texture) {
	this->normalTexture = texture;
}

RT64::Texture* RT64::Instance::getNormalTexture() const {
	return normalTexture;
}

void RT64::Instance::setTransform(float m[4][4]) {
	transform = XMMATRIX(
		m[0][0], m[0][1], m[0][2], m[0][3],
		m[1][0], m[1][1], m[1][2], m[1][3],
		m[2][0], m[2][1], m[2][2], m[2][3],
		m[3][0], m[3][1], m[3][2], m[3][3]
	);
}

XMMATRIX RT64::Instance::getTransform() const {
	return transform;
}

void RT64::Instance::setScissorRect(const RT64_RECT &rect) {
	scissorRect = rect;
}

RT64_RECT RT64::Instance::getScissorRect() const {
	return scissorRect;
}

bool RT64::Instance::hasScissorRect() const {
	return (scissorRect.w > 0) && (scissorRect.h > 0);
}

void RT64::Instance::setViewportRect(const RT64_RECT &rect) {
	viewportRect = rect;
}

RT64_RECT RT64::Instance::getViewportRect() const {
	return viewportRect;
}

bool RT64::Instance::hasViewportRect() const {
	return (viewportRect.w > 0) && (viewportRect.h > 0);
}

void RT64::Instance::setFlags(int v) {
	flags = v;
}

unsigned int RT64::Instance::getFlags() const {
	return flags;
}

// Public

DLLEXPORT RT64_INSTANCE *RT64_CreateInstance(RT64_SCENE *scenePtr) {
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	RT64::Instance *instance = new RT64::Instance(scene);
	return (RT64_INSTANCE *)(instance);
}

DLLEXPORT void RT64_SetInstanceDescription(RT64_INSTANCE *instancePtr, RT64_INSTANCE_DESC instanceDesc) {
	assert(instancePtr != nullptr);
	assert(instanceDesc.mesh != nullptr);
	assert(instanceDesc.diffuseTexture != nullptr);

	RT64::Instance *instance = (RT64::Instance *)(instancePtr);
	instance->setMesh((RT64::Mesh *)(instanceDesc.mesh));
	instance->setTransform(instanceDesc.transform.m);
	instance->setMaterial(instanceDesc.material);
	instance->setDiffuseTexture((RT64::Texture *)(instanceDesc.diffuseTexture));
	instance->setNormalTexture((RT64::Texture *)(instanceDesc.normalTexture));
	instance->setFlags(instanceDesc.flags);
	instance->setScissorRect(instanceDesc.scissorRect);
	instance->setViewportRect(instanceDesc.viewportRect);
}

DLLEXPORT void RT64_DestroyInstance(RT64_INSTANCE *instancePtr) {
	delete (RT64::Instance *)(instancePtr);
}

#endif