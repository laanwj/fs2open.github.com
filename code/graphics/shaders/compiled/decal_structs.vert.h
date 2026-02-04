
#pragma once

#include <cstdint>
#include <array>

struct genericData_decal_vert {
	SPIRV_FLOAT_MAT_4x4 modelMatrix;
	SPIRV_FLOAT_VEC4 color;
	SPIRV_FLOAT_VEC4 clipEquation;
	std::int32_t baseMapIndex;
	std::int32_t alphaTexture;
	std::int32_t noTexturing;
	std::int32_t srgb;
	float intensity;
	float alphaThreshold;
	std::int32_t clipEnabled;
};
static_assert(sizeof(genericData_decal_vert) == 124, "Size of struct genericData_decal_vert does not match what is expected for the uniform block!");
static_assert(offsetof(genericData_decal_vert, modelMatrix) == 0, "Offset of member modelMatrix does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, color) == 64, "Offset of member color does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, clipEquation) == 80, "Offset of member clipEquation does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, baseMapIndex) == 96, "Offset of member baseMapIndex does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, alphaTexture) == 100, "Offset of member alphaTexture does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, noTexturing) == 104, "Offset of member noTexturing does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, srgb) == 108, "Offset of member srgb does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, intensity) == 112, "Offset of member intensity does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, alphaThreshold) == 116, "Offset of member alphaThreshold does not match the uniform buffer offset!");
static_assert(offsetof(genericData_decal_vert, clipEnabled) == 120, "Offset of member clipEnabled does not match the uniform buffer offset!");
