
#pragma once

#include <cstdint>
#include <array>

struct genericData_batched_vert {
	SPIRV_FLOAT_VEC4 color;
	float intensity;
	uint8_t _padding0[12];
	float pad;
	uint8_t _padding1[44];
};
static_assert(sizeof(genericData_batched_vert) == 80, "Size of struct genericData_batched_vert does not match what is expected for the uniform block!");
static_assert(offsetof(genericData_batched_vert, color) == 0, "Offset of member color does not match the uniform buffer offset!");
static_assert(offsetof(genericData_batched_vert, intensity) == 16, "Offset of member intensity does not match the uniform buffer offset!");
static_assert(offsetof(genericData_batched_vert, pad) == 32, "Offset of member pad does not match the uniform buffer offset!");
struct matrixData_batched_vert {
	SPIRV_FLOAT_MAT_4x4 modelViewMatrix;
	SPIRV_FLOAT_MAT_4x4 projMatrix;
};
static_assert(sizeof(matrixData_batched_vert) == 128, "Size of struct matrixData_batched_vert does not match what is expected for the uniform block!");
static_assert(offsetof(matrixData_batched_vert, modelViewMatrix) == 0, "Offset of member modelViewMatrix does not match the uniform buffer offset!");
static_assert(offsetof(matrixData_batched_vert, projMatrix) == 64, "Offset of member projMatrix does not match the uniform buffer offset!");
