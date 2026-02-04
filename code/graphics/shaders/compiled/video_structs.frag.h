
#pragma once

#include <cstdint>
#include <array>

struct movieData_video_frag {
	float alpha;
	uint8_t _padding0[12];
	float pad;
	uint8_t _padding1[44];
};
static_assert(sizeof(movieData_video_frag) == 64, "Size of struct movieData_video_frag does not match what is expected for the uniform block!");
static_assert(offsetof(movieData_video_frag, alpha) == 0, "Offset of member alpha does not match the uniform buffer offset!");
static_assert(offsetof(movieData_video_frag, pad) == 16, "Offset of member pad does not match the uniform buffer offset!");
