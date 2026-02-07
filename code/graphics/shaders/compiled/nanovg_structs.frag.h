
#pragma once

#include <cstdint>
#include <array>

struct NanoVGUniformData_nanovg_frag {
	SPIRV_FLOAT_MAT_3x3 scissorMat;
	SPIRV_FLOAT_MAT_3x3 paintMat;
	SPIRV_FLOAT_VEC4 innerCol;
	SPIRV_FLOAT_VEC4 outerCol;
	SPIRV_FLOAT_VEC2 scissorExt;
	SPIRV_FLOAT_VEC2 scissorScale;
	SPIRV_FLOAT_VEC2 extent;
	float radius;
	float feather;
	float strokeMult;
	float strokeThr;
	std::int32_t texType;
	std::int32_t type;
	SPIRV_FLOAT_VEC2 viewSize;
	std::int32_t texArrayIndex;
};
static_assert(sizeof(NanoVGUniformData_nanovg_frag) == 188, "Size of struct NanoVGUniformData_nanovg_frag does not match what is expected for the uniform block!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, scissorMat) == 0, "Offset of member scissorMat does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, paintMat) == 48, "Offset of member paintMat does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, innerCol) == 96, "Offset of member innerCol does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, outerCol) == 112, "Offset of member outerCol does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, scissorExt) == 128, "Offset of member scissorExt does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, scissorScale) == 136, "Offset of member scissorScale does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, extent) == 144, "Offset of member extent does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, radius) == 152, "Offset of member radius does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, feather) == 156, "Offset of member feather does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, strokeMult) == 160, "Offset of member strokeMult does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, strokeThr) == 164, "Offset of member strokeThr does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, texType) == 168, "Offset of member texType does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, type) == 172, "Offset of member type does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, viewSize) == 176, "Offset of member viewSize does not match the uniform buffer offset!");
static_assert(offsetof(NanoVGUniformData_nanovg_frag, texArrayIndex) == 184, "Offset of member texArrayIndex does not match the uniform buffer offset!");
