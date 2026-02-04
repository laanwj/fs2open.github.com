
#pragma once

#include <cstdint>
#include <array>

struct genericData_batched_frag {
	SPIRV_FLOAT_VEC4 color;
	float intensity;
	uint8_t _padding0[12];
	float pad;
	uint8_t _padding1[44];
};
static_assert(sizeof(genericData_batched_frag) == 80, "Size of struct genericData_batched_frag does not match what is expected for the uniform block!");
static_assert(offsetof(genericData_batched_frag, color) == 0, "Offset of member color does not match the uniform buffer offset!");
static_assert(offsetof(genericData_batched_frag, intensity) == 16, "Offset of member intensity does not match the uniform buffer offset!");
static_assert(offsetof(genericData_batched_frag, pad) == 32, "Offset of member pad does not match the uniform buffer offset!");
