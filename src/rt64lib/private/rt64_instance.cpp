//
// RT64
//

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

// Public

DLLEXPORT RT64_INSTANCE *RT64_CreateInstance(RT64_SCENE *scenePtr) {
	RT64::Scene *scene = (RT64::Scene *)(scenePtr);
	RT64::Instance *instance = new RT64::Instance(scene);
	return (RT64_INSTANCE *)(instance);
}

DLLEXPORT void RT64_SetInstance(RT64_INSTANCE *instancePtr, RT64_MESH *meshPtr, RT64_MATRIX4 transform, RT64_TEXTURE *diffuseTexturePtr, RT64_TEXTURE* normalTexturePtr, RT64_MATERIAL material) {
	assert(instancePtr != nullptr);
	assert(meshPtr != nullptr);
	assert(diffuseTexturePtr != nullptr);

	RT64::Instance *instance = (RT64::Instance *)(instancePtr);
	RT64::Mesh *mesh = (RT64::Mesh *)(meshPtr);
	RT64::Texture *diffuseTexture = (RT64::Texture *)(diffuseTexturePtr);
	RT64::Texture* normalTexture = (RT64::Texture*)(normalTexturePtr);
	instance->setMesh(mesh);
	instance->setTransform(transform.m);
	instance->setMaterial(material);
	instance->setDiffuseTexture(diffuseTexture);
	instance->setNormalTexture(normalTexture);
}

DLLEXPORT void RT64_DestroyInstance(RT64_INSTANCE *instancePtr) {
	delete (RT64::Instance *)(instancePtr);
}