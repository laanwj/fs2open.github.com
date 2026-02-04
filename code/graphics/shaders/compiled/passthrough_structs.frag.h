
#pragma once

#include <cstdint>
#include <array>

struct genericData_passthrough_frag {
	std::int32_t noTexturing;
	std::int32_t srgb;
	uint8_t _padding0[8];
	float pad;
	uint8_t _padding1[28];
};
static_assert(sizeof(genericData_passthrough_frag) == 48, "Size of struct genericData_passthrough_frag does not match what is expected for the uniform block!");
static_assert(offsetof(genericData_passthrough_frag, noTexturing) == 0, "Offset of member noTexturing does not match the uniform buffer offset!");
static_assert(offsetof(genericData_passthrough_frag, srgb) == 4, "Offset of member srgb does not match the uniform buffer offset!");
static_assert(offsetof(genericData_passthrough_frag, pad) == 16, "Offset of member pad does not match the uniform buffer offset!");
