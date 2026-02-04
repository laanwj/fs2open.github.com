
#pragma once

#include <cstdint>
#include <array>

struct matrixData_passthrough_vert {
	SPIRV_FLOAT_MAT_4x4 modelViewMatrix;
	SPIRV_FLOAT_MAT_4x4 projMatrix;
};
static_assert(sizeof(matrixData_passthrough_vert) == 128, "Size of struct matrixData_passthrough_vert does not match what is expected for the uniform block!");
static_assert(offsetof(matrixData_passthrough_vert, modelViewMatrix) == 0, "Offset of member modelViewMatrix does not match the uniform buffer offset!");
static_assert(offsetof(matrixData_passthrough_vert, projMatrix) == 64, "Offset of member projMatrix does not match the uniform buffer offset!");
