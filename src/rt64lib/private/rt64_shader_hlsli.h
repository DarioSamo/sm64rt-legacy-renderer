//
// RT64
//

#pragma once

#define SHADER_AS_STRING

const char GlobalHitBuffersHLSLI[] =
#include "shaders/GlobalHitBuffers.hlsli"
;

const char InstancesHLSLI[] =
#include "shaders/Instances.hlsli"
;

const char MaterialsHLSLI[] =
#include "shaders/Materials.hlsli"
;

const char RandomHLSLI[] =
#include "shaders/Random.hlsli"
;

const char RayHLSLI[] =
#include "shaders/Ray.hlsli"
;

const char TexturesHLSLI[] =
#include "shaders/Textures.hlsli"
;

const char ViewParamsHLSLI[] =
#include "shaders/ViewParams.hlsli"
;

#define INCLUDE_HLSLI(x) &x[strlen("#else\n")]